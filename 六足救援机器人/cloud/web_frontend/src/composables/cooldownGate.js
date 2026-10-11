export function createCooldownGate(intervalMs, now = () => Date.now()) {
  const interval = Number.isFinite(intervalMs) ? Math.max(0, intervalMs) : 0
  let lastAcceptedAt = null

  function tryAccept() {
    const current = Number(now())
    if (!Number.isFinite(current)) return false
    if (lastAcceptedAt != null) {
      const elapsed = Math.max(0, current - lastAcceptedAt)
      if (elapsed < interval) return false
    }
    lastAcceptedAt = current
    return true
  }

  return { tryAccept }
}
