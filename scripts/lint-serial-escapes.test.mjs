// Self-test for the serial-escape lint. Zero-dependency (node:test + node:assert), run with
//   node --test scripts/lint-serial-escapes.test.mjs
// Each string below is written with real backslashes, so what the test sees is what a
// source file on disk contains.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scanText } from './lint-serial-escapes.mjs';

test('flags a doubled-backslash newline in a log call', () => {
  const findings = scanText('Serial.printf("[Tag] hello\\\\n", x);');
  assert.equal(findings.length, 1);
  assert.match(findings[0].literal, /\\\\n/);
});

test('flags doubled \\r and \\t too', () => {
  assert.equal(scanText('f("a\\\\r");').length, 1);
  assert.equal(scanText('f("a\\\\t");').length, 1);
});

test('allows a literal that is exactly one escape (the JSON-escape idiom)', () => {
  assert.equal(scanText('else if (ch == 0) file.print("\\\\n");').length, 0);
  assert.equal(scanText('file.print("\\\\r");').length, 0);
  assert.equal(scanText('file.print("\\\\t");').length, 0);
});

test('ignores a correct single-backslash newline', () => {
  assert.equal(scanText('Serial.printf("[Tag] ok\\n");').length, 0);
});

test('reports the correct line number across multiple lines', () => {
  const text = 'line one\nSerial.printf("bug\\\\n");\nline three\n';
  const findings = scanText(text);
  assert.equal(findings.length, 1);
  assert.equal(findings[0].line, 2);
});

test('finds every occurrence in a file', () => {
  const text = 'f("a\\\\n");\nf("b\\\\n");\nf("\\\\n");\n'; // two bugs + one allowed idiom
  assert.equal(scanText(text).length, 2);
});
