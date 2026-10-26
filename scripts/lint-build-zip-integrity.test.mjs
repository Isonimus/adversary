// Self-test for the build-ZIP integrity gate. Zero-dependency (node:test + node:assert), run
// with
//   node --test scripts/lint-build-zip-integrity.test.mjs
// Only the pure logic is exercised (parsePinnedUrls, parseChecksums, crossCheck,
// compareHashes); the network download + hashing in main() is IO and is covered by running
// the lint against the real URLs in CI. See ADR-0125.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parsePinnedUrls, parseChecksums, crossCheck, compareHashes } from './lint-build-zip-integrity.mjs';

const URL_A = 'https://github.com/pioarduino/platform-espressif32/releases/download/55.03.34/platform-espressif32.zip';
const URL_B = 'https://github.com/bmorcelli/esp32-arduino-lib-builder/releases/download/idf-release_v5.5/bruce.zip';
const HEX = 'a'.repeat(64);

test('parsePinnedUrls extracts release-asset archive URLs and dedupes (both envs pin the same URL)', () => {
  const ini = `platform = ${URL_A}\nplatform_packages =\n  x @ ${URL_B}\n[env:release]\nplatform = ${URL_A}\n`;
  assert.deepEqual(parsePinnedUrls(ini), [URL_A, URL_B].sort());
});

test('parsePinnedUrls ignores registry lib_deps (owner/name@version, not URLs)', () => {
  const ini = 'lib_deps =\n  bblanchon/ArduinoJson@7.4.3\n  h2zero/NimBLE-Arduino@1.4.3\n';
  assert.deepEqual(parsePinnedUrls(ini), []);
});

test('parsePinnedUrls matches .tar.gz and .tar.xz archives too', () => {
  const ini = `a = https://github.com/o/r/releases/download/v1/core.tar.xz\nb = https://github.com/o/r/releases/download/v1/libs.tar.gz\n`;
  assert.equal(parsePinnedUrls(ini).length, 2);
});

test('parseChecksums maps url -> sha256 value', () => {
  const map = parseChecksums(JSON.stringify({ checksums: { [URL_A]: `sha256:${HEX}` } }));
  assert.equal(map.get(URL_A), `sha256:${HEX}`);
});

test('parseChecksums throws on a malformed hash value (fail loud, never default-pass)', () => {
  assert.throws(() => parseChecksums(JSON.stringify({ checksums: { [URL_A]: 'deadbeef' } })), /sha256/);
});

test('parseChecksums throws when the "checksums" object is absent', () => {
  assert.throws(() => parseChecksums(JSON.stringify({ notChecksums: {} })), /checksums/);
});

test('crossCheck flags a pinned URL with no checksum entry (a bump cannot bypass the gate)', () => {
  const r = crossCheck([URL_A, URL_B], [URL_A]);
  assert.deepEqual(r.missing, [URL_B]);
  assert.deepEqual(r.stale, []);
});

test('crossCheck flags a stale checksum entry no longer pinned', () => {
  const r = crossCheck([URL_A], [URL_A, URL_B]);
  assert.deepEqual(r.missing, []);
  assert.deepEqual(r.stale, [URL_B]);
});

test('crossCheck passes when the two sets match', () => {
  const r = crossCheck([URL_A, URL_B], [URL_B, URL_A]);
  assert.deepEqual(r.missing, []);
  assert.deepEqual(r.stale, []);
});

test('compareHashes reports a changed asset', () => {
  const checksums = new Map([[URL_A, `sha256:${HEX}`]]);
  const actual = new Map([[URL_A, `sha256:${'b'.repeat(64)}`]]);
  const m = compareHashes(checksums, actual);
  assert.equal(m.length, 1);
  assert.equal(m[0].url, URL_A);
});

test('compareHashes passes when every hash matches', () => {
  const checksums = new Map([[URL_A, `sha256:${HEX}`]]);
  const actual = new Map([[URL_A, `sha256:${HEX}`]]);
  assert.deepEqual(compareHashes(checksums, actual), []);
});

test('compareHashes reports a url that was never fetched rather than silently passing', () => {
  const checksums = new Map([[URL_A, `sha256:${HEX}`]]);
  const m = compareHashes(checksums, new Map());
  assert.equal(m.length, 1);
  assert.equal(m[0].actual, '(not fetched)');
});
