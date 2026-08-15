// A minimal, deliberately non-conforming RFC 6455 client built directly on `net.Socket`,
// for tests that must send a frame after the server has already decided to eject the
// connection. A real `ws`/WebSocket client refuses `send()` the instant it observes
// CLOSING, which makes the ejection-guard and pre-hello-slot-release behaviours
// unreachable through it: both need a peer that keeps writing (or simply keeps its TCP
// connection open) after the server's close() has already run. This module does the HTTP
// upgrade handshake and raw frame masking by hand so tests can do exactly that.
//
// Not a general-purpose WebSocket client: no ping/pong, no fragmentation, no close
// handshake reply. Just enough of RFC 6455 to open a connection and push masked frames.

import net from 'node:net';
import crypto from 'node:crypto';

const OPCODE = { text: 0x1, binary: 0x2, close: 0x8, ping: 0x9, pong: 0xa };

function encodeFrame(opcode, payload) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.from([0x80 | opcode, 0x80 | len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 0x80 | 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 0x80 | 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  const maskKey = crypto.randomBytes(4);
  const masked = Buffer.alloc(len);
  for (let i = 0; i < len; i += 1) masked[i] = payload[i] ^ maskKey[i % 4];
  return Buffer.concat([header, maskKey, masked]);
}

/**
 * Opens a raw TCP connection, performs the WebSocket opening handshake, and resolves with
 * a client exposing `sendText`/`sendBinary`/`sendRawBytes` (unmasked-frame writes that
 * bypass any client-side notion of connection state) and `dispose`.
 *
 * @param {number} port
 * @param {{room?: string}} [options] unused placeholder kept for symmetry with connect()
 * @returns {Promise<{socket: net.Socket, frames: Array, sendText: Function,
 *   sendBinary: Function, sendRawBytes: Function, dispose: Function}>}
 */
export function connectRaw(port) {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, '127.0.0.1');
    let settled = false;
    let handshakeDone = false;
    let buf = Buffer.alloc(0);
    const frames = [];

    const fail = (err) => {
      if (settled) return;
      settled = true;
      reject(err);
    };

    socket.on('connect', () => {
      const key = crypto.randomBytes(16).toString('base64');
      const request =
        `GET / HTTP/1.1\r\n` +
        `Host: 127.0.0.1:${port}\r\n` +
        `Upgrade: websocket\r\n` +
        `Connection: Upgrade\r\n` +
        `Sec-WebSocket-Key: ${key}\r\n` +
        `Sec-WebSocket-Version: 13\r\n\r\n`;
      socket.write(request);
    });

    socket.on('error', fail);

    socket.on('data', (chunk) => {
      buf = Buffer.concat([buf, chunk]);

      if (!handshakeDone) {
        const headerEnd = buf.indexOf('\r\n\r\n');
        if (headerEnd === -1) return;
        const statusLine = buf.subarray(0, headerEnd).toString('latin1').split('\r\n')[0];
        buf = buf.subarray(headerEnd + 4);
        handshakeDone = true;
        if (!/^HTTP\/1\.1 101\b/.test(statusLine)) {
          fail(new Error(`raw handshake refused: ${statusLine}`));
          return;
        }
        if (!settled) {
          settled = true;
          resolve(client);
        }
      }

      // Server frames are sent unmasked; parse as many complete frames as are buffered.
      for (;;) {
        if (buf.length < 2) return;
        const opcode = buf[0] & 0x0f;
        let len = buf[1] & 0x7f;
        let offset = 2;
        if (len === 126) {
          if (buf.length < 4) return;
          len = buf.readUInt16BE(2);
          offset = 4;
        } else if (len === 127) {
          if (buf.length < 10) return;
          len = Number(buf.readBigUInt64BE(2));
          offset = 10;
        }
        if (buf.length < offset + len) return;
        frames.push({ opcode, payload: Buffer.from(buf.subarray(offset, offset + len)) });
        buf = buf.subarray(offset + len);
      }
    });

    const client = {
      socket,
      frames,
      sendText(obj) {
        socket.write(encodeFrame(OPCODE.text, Buffer.from(JSON.stringify(obj), 'utf8')));
      },
      sendBinary(bytes) {
        socket.write(encodeFrame(OPCODE.binary, Buffer.from(bytes)));
      },
      /** Writes a masked frame of the given opcode with an arbitrary payload. */
      sendRawBytes(opcode, bytes) {
        socket.write(encodeFrame(opcode, Buffer.from(bytes)));
      },
      dispose() {
        socket.destroy();
      },
    };
  });
}

export { OPCODE };
