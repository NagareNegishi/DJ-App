// Single-line JSON logging to stdout, per docs/plan/03-server.md §Logging.
// A dumb sink: it decides nothing about what is safe to log (callers are bound by the
// redaction rules) and it never throws, because a logging bug must not take down a connection.

const LEVELS = { error: 0, warn: 1, info: 2, debug: 3 };

const defaultWrite = (line) => process.stdout.write(line);

/**
 * Creates a leveled logger.
 *
 * Returns one function per level, each taking a short stable event slug ('client-joined',
 * 'rate-limited', …) and an optional field bag, and emitting exactly one JSON line.
 *
 * @param {{level?: 'error'|'warn'|'info'|'debug', write?: (line: string) => void,
 *   now?: () => Date}} [options]
 * @returns {{error: Function, warn: Function, info: Function, debug: Function}}
 */
export function createLogger({ level = 'info', write = defaultWrite, now = () => new Date() } = {}) {
  const threshold = LEVELS[level] ?? LEVELS.info;

  const emit = (callLevel) => (event, fields = {}) => {
    if (LEVELS[callLevel] > threshold) return;

    const ts = now().toISOString();
    let line;
    try {
      line = JSON.stringify({ ts, level: callLevel, event, ...fields });
    } catch {
      // Circular or otherwise unserializable fields: keep the event, drop the payload.
      line = JSON.stringify({ ts, level: callLevel, event: String(event), logError: 'unserializable fields' });
    }
    write(`${line}\n`);
  };

  return { error: emit('error'), warn: emit('warn'), info: emit('info'), debug: emit('debug') };
}
