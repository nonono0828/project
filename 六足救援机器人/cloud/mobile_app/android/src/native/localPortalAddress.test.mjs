import assert from 'node:assert/strict'
import test from 'node:test'

import {
  DEFAULT_LOCAL_PORTAL_ADDRESS,
  normalizeDesktopPortalAddress,
  normalizeLocalPortalAddress,
} from './localPortalAddress.js'

test('normalizes the supported robot portal host', () => {
  assert.equal(
    normalizeLocalPortalAddress('192.168.4.1:8081'),
    DEFAULT_LOCAL_PORTAL_ADDRESS,
  )
  assert.equal(
    normalizeLocalPortalAddress('http://192.168.4.1:8081/api'),
    'http://192.168.4.1:8081/api',
  )
})

test('rejects empty, credentialed, encrypted, and foreign hosts', () => {
  assert.throws(() => normalizeLocalPortalAddress(''), /请输入/)
  assert.throws(
    () => normalizeLocalPortalAddress('http://user:pass@192.168.4.1:8081'),
    /不能包含用户名或密码/,
  )
  assert.throws(
    () => normalizeLocalPortalAddress('https://192.168.4.1:8081'),
    /仅支持 HTTP/,
  )
  assert.throws(
    () => normalizeLocalPortalAddress('http://192.168.40.1:8081'),
    /192\.168\.4\.1/,
  )
})

test('desktop accepts any valid HTTP configuration host without weakening Android rules', () => {
  assert.equal(
    normalizeDesktopPortalAddress('192.168.40.1:9090/config'),
    'http://192.168.40.1:9090/config',
  )
  assert.equal(
    normalizeDesktopPortalAddress('robot.local:8081'),
    'http://robot.local:8081/',
  )
  assert.equal(
    normalizeDesktopPortalAddress('https://robot.local/config'),
    'https://robot.local/config',
  )
  assert.throws(
    () => normalizeLocalPortalAddress('192.168.40.1:9090/config'),
    /192\.168\.4\.1/,
  )
})

test('desktop still rejects empty, credentialed, encrypted, and malformed addresses', () => {
  assert.throws(() => normalizeDesktopPortalAddress(''), /请输入/)
  assert.throws(
    () => normalizeDesktopPortalAddress('http://user:pass@robot.local:8081'),
    /不能包含用户名或密码/,
  )
  assert.throws(() => normalizeDesktopPortalAddress('ftp://robot.local'), /HTTP 或 HTTPS/)
  assert.throws(() => normalizeDesktopPortalAddress('http://['), /格式不正确/)
})
