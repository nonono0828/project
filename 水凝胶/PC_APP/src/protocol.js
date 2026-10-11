export function parseDeviceLine(rawLine) {
  if (typeof rawLine !== 'string') return null;
  const line = rawLine.replace(/\r/g, '').trim();
  if (!line || line.length > 128) return null;

  const parts = line.split(',').map((part) => part.trim());
  const command = parts[0].toUpperCase();

  if (command === 'DEVICE' && parts.length >= 3) {
    return { type: 'device', name: parts[1], version: parts[2], raw: line };
  }

  if (command === 'KEY' && parts.length === 4) {
    const key = Number(parts[1]);
    const action = parts[2].toUpperCase();
    const pressure = Number(parts[3]);
    if (!Number.isInteger(key) || key < 1 || key > 9) return null;
    if (action !== 'DOWN' && action !== 'UP') return null;
    if (!Number.isFinite(pressure)) return null;
    return {
      type: 'key',
      key,
      action,
      pressure: Math.max(0, Math.round(pressure)),
      raw: line
    };
  }

  if (command === 'HEARTBEAT' && parts.length === 3) {
    const key = Number(parts[1]);
    const pressure = Number(parts[2]);
    if (!Number.isInteger(key) || key < 0 || key > 9) return null;
    if (!Number.isFinite(pressure)) return null;
    return {
      type: 'heartbeat',
      key,
      pressure: Math.max(0, Math.round(pressure)),
      raw: line
    };
  }

  return { type: 'unknown', raw: line };
}

export function formatPortLabel(port) {
  const path = port?.path || '未知端口';
  const detail = port?.friendlyName || port?.manufacturer || '';
  return detail && !detail.includes(path) ? `${path} · ${detail}` : path;
}
