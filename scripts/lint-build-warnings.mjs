#!/usr/bin/env node
// Gate: no NEW compiler warnings in our own src/. Slices 0081 and 0082 brought our src/ to
// zero warnings on both device builds; this keeps it there (ADR-0083).
//
//   node scripts/lint-build-warnings.mjs [build.log]      (default: build.log)
//
// Zero dependencies by design, like the other scripts/*.mjs gates. Exit 1 on any finding.
//
// Mechanism: scan a captured PlatformIO build log and keep the `warning:` lines whose source
// path is OURS -- under src/ and not in a vendored in-src/ subtree. Everything else (the
// framework, lib/, and the vendored trees that live inside src/) is out of scope BY PATH, the
// same way the cppcheck gate scopes to src/ (slices/0011). We do NOT use -Werror: measured
// 2026-10-20, our BLE TUs pull NimBLE's nimconfig.h, which redefines CONFIG_BT_NIMBLE_* macros
// the framework sdkconfig.h already defines; under -Werror those become errors tagged bare
// [-Werror] with no class name, so no -Wno-error= and no #pragma can suppress them. Scoping by
// path sidesteps that entirely -- we never ask the compiler to ignore a vendored warning, we
// just never attribute it to us. Full rationale: ADR-0083.
//
// CI does not cache .pio/build, so every CI build is cold and every our-src warning is in the
// log; the gate is authoritative there. Locally, .pio/build is incremental, so run it after a
// clean build to see every TU.

import { existsSync, readFileSync, statSync } from 'node:fs';

// Vendored code that lives inside src/. A warning on one of these paths is not ours. This is
// the same exclusion set slices 0081/0082 used to count our own warnings. Prefixes match a
// whole directory; M5Unified.cpp is a single vendored file dropped into src/.
const VENDORED_SRC_PREFIXES = [
  'src/common/',
  'src/fl/',
  'src/lgfx/',
  'src/class/',
  'src/platforms/',
  'src/third_party/',
];
const VENDORED_SRC_FILES = new Set(['src/M5Unified.cpp']);

// One GCC/Clang warning line: "<path>:<line>[:<col>]: warning: <message>". The path is matched
// non-greedily so it stops at the first ":<digits>:" that begins the location, not at a colon
// inside the message. Note lines ("note:"), "In file included from" context lines, and error
// lines do not match, so only true warning lines are considered.
const WARNING_LINE = /^(.+?):(\d+)(?::(\d+))?: warning: (.*)$/;

/** True if a source path belongs to our own code (under src/, not a vendored in-src/ tree). */
export function isOurSrcPath(path) {
  if (!path.startsWith('src/')) return false;
  if (VENDORED_SRC_FILES.has(path)) return false;
  return !VENDORED_SRC_PREFIXES.some((prefix) => path.startsWith(prefix));
}

/**
 * Scan a build log's text; return one finding per warning on an our-src path.
 * Exported for the self-test. Pure: no I/O, no process state.
 */
export function scanLog(text) {
  const findings = [];
  for (const rawLine of text.split('\n')) {
    const match = WARNING_LINE.exec(rawLine);
    if (!match) continue;
    const path = match[1].replace(/^\.\//, '');
    if (!isOurSrcPath(path)) continue;
    findings.push({
      path,
      line: Number(match[2]),
      col: match[3] ? Number(match[3]) : 0,
      message: match[4],
    });
  }
  return findings;
}

function main(argv) {
  const logPath = argv[0] ?? 'build.log';

  // Fail loud on a log we cannot read: the gate must not certify absence of warnings from a log
  // that was never produced (a wrong path, a build that never ran). Mirrors the size step's
  // "no Flash: line -> error" guard (slices/0039).
  if (!existsSync(logPath)) {
    console.error(`lint-build-warnings: build log not found: ${logPath}`);
    console.error('Pass a captured build log, e.g. `pio run -e cardputer 2>&1 | tee build.log`.');
    return 1;
  }
  if (statSync(logPath).size === 0) {
    console.error(`lint-build-warnings: build log is empty: ${logPath}`);
    return 1;
  }

  const findings = scanLog(readFileSync(logPath, 'utf8'));
  if (findings.length === 0) {
    console.log(`lint-build-warnings: no warnings in our own src/ (${logPath}).`);
    return 0;
  }

  for (const f of findings) {
    const col = f.col ? `:${f.col}` : '';
    console.error(`${f.path}:${f.line}${col}: warning: ${f.message}`);
  }
  console.error(
    `\nlint-build-warnings: ${findings.length} new warning(s) in our own src/. ` +
    'Fix them -- our src/ is warning-clean and must stay so (ADR-0083).');
  return 1;
}

// Run only as a CLI, not when imported by the test.
if (import.meta.url === `file://${process.argv[1]}`) {
  process.exit(main(process.argv.slice(2)));
}
