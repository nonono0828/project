import assert from 'node:assert/strict'
import { createCooldownGate } from './cooldownGate.js'

let now = 1000
const gate = createCooldownGate(10_000, () => now)

assert.equal(gate.tryAccept(), true)
now += 9999
assert.equal(gate.tryAccept(), false, 'the cooldown must reject events before 10 seconds')
now += 1
assert.equal(gate.tryAccept(), true, 'the cooldown boundary must accept a new event')
now -= 5000
assert.equal(gate.tryAccept(), false, 'a backward clock adjustment must not bypass the cooldown')

console.log('cooldown gate: all assertions passed')
