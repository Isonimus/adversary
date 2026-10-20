// Self-test for the dependency-lock staleness gate. Zero-dependency (node:test +
// node:assert), run with
//   node --test scripts/lint-dep-lock-staleness.test.mjs
// Only the pure decision is exercised here (parseLockDate + evaluate); the git lookup in
// main() is IO and is covered by running the lint against the real repo in CI.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseLockDate, evaluate } from './lint-dep-lock-staleness.mjs';

test('parseLockDate pulls the ISO date from the stamp line', () => {
  assert.equal(parseLockDate('Last verified: 2026-10-20 — native 844/844, builds SUCCESS.'), '2026-10-20');
});

test('parseLockDate throws when the stamp line is missing (fail loud, never default-pass)', () => {
  assert.throws(() => parseLockDate('# Dependency lock\n\nno stamp here\n'), /Last verified/);
});

test('flags a lock older than the latest release tag, naming both', () => {
  const r = evaluate({ lockDate: '2026-09-08', tagDate: '2026-10-19', tagName: 'v0.4.0-alpha' });
  assert.equal(r.stale, true);
  assert.match(r.message, /2026-09-08/);
  assert.match(r.message, /v0\.4\.0-alpha/);
  assert.match(r.message, /2026-10-19/);
});

test('passes a lock newer than the latest release tag', () => {
  assert.equal(evaluate({ lockDate: '2026-10-20', tagDate: '2026-10-19', tagName: 'v0.4.0-alpha' }).stale, false);
});

test('passes same-day (lock re-verified on the release date)', () => {
  assert.equal(evaluate({ lockDate: '2026-10-19', tagDate: '2026-10-19', tagName: 'v0.4.0-alpha' }).stale, false);
});

test('passes when there are no release tags to compare against', () => {
  const r = evaluate({ lockDate: '2026-10-20', tagDate: null, tagName: null });
  assert.equal(r.stale, false);
  assert.match(r.message, /no release tags/);
});
