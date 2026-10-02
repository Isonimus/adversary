#!/usr/bin/env node
// Flags a literal `\n` / `\r` / `\t` accidentally written into a string literal as a
// DOUBLED backslash ("...\\n"), which emits the two characters backslash+n at runtime
// instead of a newline. This smears serial logs into run-on records. See slices/0037.
//
//   node scripts/lint-serial-escapes.mjs [dir ...]     (default: src)
//
// Zero dependencies by design, like the other scripts/*.mjs — drops into any checkout
// without an install step. Exit 1 if any finding.
//
// Rule (content-based, call-agnostic): flag a string literal whose content contains the
// two-character sequence `\\n`, `\\r`, or `\\t` — UNLESS the literal is exactly that one
// escape ("\\n" / "\\r" / "\\t"). The lone form is the deliberate "render a control byte
// as its escape text" idiom (e.g. JSON escaping in traffic_proxy.cpp). Keying off literal
// content rather than the surrounding call is what lets the rule catch
// `Serial.printf("x\\n")` while sparing `file.print("\\n")` — both exist in this tree.
//
// Known limit: a longer literal that *intends* to display an escape (e.g. help text
// "use \\n for newline") would be flagged. None exist today; if one arises, widen the
// allow-form here rather than loosening the gate (same stance as the cppcheck job).

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, extname, relative } from 'node:path';

const SOURCE_EXTS = new Set(['.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx']);

// One C/C++ string literal: opening quote, then escaped chars or any non-quote/backslash,
// up to the closing quote. Excludes raw newlines so an unterminated quote cannot run away.
const STRING_LITERAL = /"(?:\\.|[^"\\\n])*"/g;
const BAD_ESCAPE = /\\\\[nrt]/;      // backslash backslash (n|r|t), anywhere in the literal
const LONE_ESCAPE = /^"\\\\[nrt]"$/; // the entire literal is exactly that escape — allowed

/** Scan one file's text; return findings with 1-based line/column and the offending literal. */
export function scanText(text) {
  const findings = [];
  const lineStarts = [0];
  for (let i = 0; i < text.length; i++) {
    if (text[i] === '\n') lineStarts.push(i + 1);
  }
  const locate = (index) => {
    let lo = 0;
    let hi = lineStarts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (lineStarts[mid] <= index) lo = mid;
      else hi = mid - 1;
    }
    return { line: lo + 1, col: index - lineStarts[lo] + 1 };
  };

  for (const match of text.matchAll(STRING_LITERAL)) {
    const literal = match[0];
    if (!BAD_ESCAPE.test(literal) || LONE_ESCAPE.test(literal)) continue;
    findings.push({ ...locate(match.index), literal });
  }
  return findings;
}

function collectSources(dir) {
  const out = [];
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) out.push(...collectSources(path));
    else if (SOURCE_EXTS.has(extname(entry))) out.push(path);
  }
  return out;
}

function main(argv) {
  const dirs = argv.length > 0 ? argv : ['src'];
  let total = 0;
  for (const dir of dirs) {
    for (const file of collectSources(dir)) {
      for (const f of scanText(readFileSync(file, 'utf8'))) {
        total++;
        console.error(`${relative('.', file)}:${f.line}:${f.col}: literal escape in string: ${f.literal}`);
      }
    }
  }
  if (total > 0) {
    console.error(`\nlint-serial-escapes: ${total} literal-escape ${total === 1 ? 'bug' : 'bugs'} found.`);
    console.error('A doubled backslash ("\\\\n") emits two characters, not a newline. Use a single "\\n".');
    return 1;
  }
  return 0;
}

// Run only as a CLI, not when imported by the test.
if (import.meta.url === `file://${process.argv[1]}`) {
  process.exit(main(process.argv.slice(2)));
}
