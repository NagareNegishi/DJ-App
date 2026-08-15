// The room domain: membership, control ownership, canonical deck state, and what each
// protocol message does to them. Deliberately free of any `ws` import — it never learns
// that sockets exist, and works on the `conn` abstraction server.js injects, which is what
// makes it testable without a network.

import { randomBytes } from 'node:crypto';

import { PROTOCOL_VERSION, DEFAULT_DECK_STATE, ERROR_CODES, CLOSE_CODES } from './protocol.js';
import { validateHello, validateDelta, validateEnvelope } from './validate.js';

const CLIENT_ID_ATTEMPTS = 100;

/** Close code to use for a hello whose validation failed, keyed by validate.js reason. */
const HANDSHAKE_CLOSE_CODES = {
  'protocol version mismatch': CLOSE_CODES.versionMismatch,
  'wrong room': CLOSE_CODES.wrongRoom,
};

const isPlainObject = (value) => typeof value === 'object' && value !== null && !Array.isArray(value);

/** Copy one deck's state, detaching the nested loop object so nothing aliases room state. */
const copyDeck = (deck) => ({ ...deck, loop: deck.loop === null ? null : { ...deck.loop } });

/**
 * Creates a room: the single authoritative state for one server process.
 *
 * `roomCode` is compared against each hello and never logged or echoed afterwards.
 */
export function createRoom({ roomCode, maxClients, logger, protocolVersion = PROTOCOL_VERSION }) {
  const clients = new Map();
  const decks = { A: copyDeck(DEFAULT_DECK_STATE) };
  let serverSeq = 0;
  let controllerId = null;

  // Role is derived from controllerId alone; storing it on conn as well would be a second
  // writer of the same fact, free to drift out of agreement with getControllerId().
  const roleOf = (conn) => (conn.id !== null && conn.id === controllerId ? 'controller' : 'observer');

  // True only for a conn that is the live entry clients has for its id. A conn whose id was
  // handed out but which handleDisconnect has since retired still has a non-null conn.id, so
  // callers that mutate room state must check this, not just conn.id !== null.
  const isMember = (conn) => conn.id !== null && clients.get(conn.id) === conn;

  /** Fans out to every registered client except `except`. conn.send is total, so no guard. */
  const broadcast = (obj, { except } = {}) => {
    for (const client of clients.values()) {
      if (client !== except) client.send(obj);
    }
  };

  const snapshotOf = () => ({
    serverSeq,
    decks: Object.fromEntries(Object.entries(decks).map(([id, deck]) => [id, copyDeck(deck)])),
  });

  const sendError = (conn, code, message) => conn.send({ type: 'error', code, message });

  /** Draws an unused `c-<4 hex>` id, or null if the (unreachable) collision cap is hit. */
  const allocateClientId = () => {
    for (let attempt = 0; attempt < CLIENT_ID_ATTEMPTS; attempt += 1) {
      const id = `c-${randomBytes(2).toString('hex')}`;
      if (!clients.has(id)) return id;
    }
    return null;
  };

  const closeHandshake = (conn, closeCode) => {
    logger.info('handshake-failed', { closeCode, remoteAddress: conn.remoteAddress });
    conn.close(closeCode);
    return { ok: false };
  };

  const handleHello = (conn, msg) => {
    const result = validateHello(msg, { protocolVersion, roomCode });
    if (!result.ok) {
      return closeHandshake(conn, HANDSHAKE_CLOSE_CODES[result.reason] ?? CLOSE_CODES.badHandshake);
    }

    // Capacity is checked only after the hello validates, so that a 4002 can never confirm
    // a guessed room code to someone probing for it.
    if (clients.size >= maxClients) return closeHandshake(conn, CLOSE_CODES.roomFull);

    const clientId = allocateClientId();
    if (clientId === null) {
      logger.error('client-id-exhausted', { remoteAddress: conn.remoteAddress });
      conn.close(CLOSE_CODES.internalError);
      return { ok: false };
    }

    conn.id = clientId;
    conn.name = result.value.name;
    clients.set(clientId, conn);

    const peers = [];
    for (const client of clients.values()) {
      if (client !== conn) peers.push({ clientId: client.id, name: client.name, role: roleOf(client) });
    }

    const role = roleOf(conn);
    conn.send({
      type: 'welcome',
      clientId,
      role,
      serverSeq,
      snapshot: { decks: snapshotOf().decks },
      peers,
    });
    broadcast({ type: 'peerJoined', clientId, name: conn.name, role }, { except: conn });

    logger.info('client-joined', { clientId, name: conn.name, remoteAddress: conn.remoteAddress });
    return { ok: true };
  };

  const handleClaimControl = (conn) => {
    if (!isMember(conn)) {
      sendError(conn, ERROR_CODES.badMessage, 'not a room member');
      return { ok: false, errorCode: ERROR_CODES.badMessage };
    }
    if (controllerId !== null) {
      sendError(conn, ERROR_CODES.controlTaken, 'control is already claimed');
      return { ok: false, errorCode: ERROR_CODES.controlTaken };
    }
    controllerId = conn.id;
    broadcast({ type: 'roleChanged', clientId: conn.id, role: 'controller' });
    logger.info('role-changed', { clientId: conn.id, role: 'controller' });
    return { ok: true };
  };

  const handleReleaseControl = (conn) => {
    if (!isMember(conn)) {
      sendError(conn, ERROR_CODES.badMessage, 'not a room member');
      return { ok: false, errorCode: ERROR_CODES.badMessage };
    }
    if (controllerId !== conn.id) {
      sendError(conn, ERROR_CODES.notController, 'only the controller can release control');
      return { ok: false, errorCode: ERROR_CODES.notController };
    }
    controllerId = null;
    broadcast({ type: 'roleChanged', clientId: conn.id, role: 'observer' });
    logger.info('role-changed', { clientId: conn.id, role: 'observer' });
    return { ok: true };
  };

  const handleDelta = (conn, msg) => {
    if (!isMember(conn)) {
      sendError(conn, ERROR_CODES.badMessage, 'not a room member');
      return { ok: false, errorCode: ERROR_CODES.badMessage };
    }

    const result = validateDelta(msg);
    if (!result.ok) {
      sendError(conn, ERROR_CODES.badMessage, result.reason);
      return { ok: false, errorCode: ERROR_CODES.badMessage };
    }

    if (conn.id !== controllerId) {
      sendError(conn, ERROR_CODES.notController, 'only the controller can change playback');
      // Repairs every rejected delta, not just the first at a given serverSeq: the sender's
      // optimistic state moves with each delta it has applied locally, even though serverSeq
      // does not move on a rejection, so a dedup keyed on serverSeq would leave every delta
      // after the first at that sequence unrepaired. The amplification cost of one snapshot
      // per rejection is accepted; server.js's per-connection rate limiter is the bound.
      conn.send({ type: 'snapshot', ...snapshotOf() });
      return { ok: false, errorCode: ERROR_CODES.notController };
    }

    const { deck, changes } = result.value;
    if (decks[deck] === undefined) decks[deck] = copyDeck(DEFAULT_DECK_STATE);
    const target = decks[deck];
    for (const [field, value] of Object.entries(changes)) {
      target[field] = field === 'loop' && value !== null ? { ...value } : value;
    }
    serverSeq += 1;

    broadcast({ type: 'delta', serverSeq, sourceClientId: conn.id, deck, changes }, { except: conn });
    logger.debug('delta-applied', { deck, fields: Object.keys(changes) });
    return { ok: true };
  };

  const handleRequestSnapshot = (conn) => {
    // Always answered, same as the not-controller repair snapshot: a client asking for state
    // is the defensive resync path. Metered by the rate limiter only.
    conn.send({ type: 'snapshot', ...snapshotOf() });
    return { ok: true };
  };

  return {
    /**
     * Applies one already-parsed, untrusted inbound message.
     *
     * Returns the domain fact only: `{ok:true}`, `{ok:false, errorCode}` when an error frame
     * was sent, or `{ok:false}` with no code when the room closed the connection itself.
     * What a rejection costs the connection is server.js's policy, not the room's.
     */
    handleMessage(conn, msg) {
      // Guard the shape before reading any property: JSON.parse('null') is a legal frame,
      // and msg.type on it would throw, making hostile input look like our own bug.
      if (!isPlainObject(msg)) {
        if (conn.id === null) return closeHandshake(conn, CLOSE_CODES.badHandshake);
        sendError(conn, ERROR_CODES.badMessage, 'not an object');
        return { ok: false, errorCode: ERROR_CODES.badMessage };
      }

      if (conn.id === null) {
        if (msg.type !== 'hello') return closeHandshake(conn, CLOSE_CODES.badHandshake);
        return handleHello(conn, msg);
      }

      const envelope = validateEnvelope(msg);
      if (!envelope.ok) {
        sendError(conn, ERROR_CODES.badMessage, envelope.reason);
        return { ok: false, errorCode: ERROR_CODES.badMessage };
      }

      switch (envelope.value.type) {
        case 'hello':
          sendError(conn, ERROR_CODES.badMessage, 'already registered');
          return { ok: false, errorCode: ERROR_CODES.badMessage };
        case 'claimControl':
          return handleClaimControl(conn);
        case 'releaseControl':
          return handleReleaseControl(conn);
        case 'delta':
          return handleDelta(conn, msg);
        case 'requestSnapshot':
          return handleRequestSnapshot(conn);
        default:
          sendError(conn, ERROR_CODES.badMessage, 'unknown type');
          return { ok: false, errorCode: ERROR_CODES.badMessage };
      }
    },

    /** Removes a client and, if it held control, frees control. Safe to call twice. */
    handleDisconnect(conn) {
      // Identity, not just id: a second call for an already-departed conn must not evict
      // the live client that has since drawn the same id.
      if (conn.id === null || clients.get(conn.id) !== conn) return;
      const { id } = conn;
      clients.delete(id);
      broadcast({ type: 'peerLeft', clientId: id });
      if (controllerId === id) {
        controllerId = null;
        // peerLeft already implies the departed client is no longer controller; the
        // explicit roleChanged gives clients that only track roles a "control is free"
        // signal. No auto-promotion — a human claims it.
        broadcast({ type: 'roleChanged', clientId: id, role: 'observer' });
        logger.info('role-changed', { clientId: id, role: 'observer' });
      }
      logger.info('client-left', { clientId: id });
    },

    clientCount() {
      return clients.size;
    },

    getSnapshot() {
      return snapshotOf();
    },

    getControllerId() {
      return controllerId;
    },
  };
}
