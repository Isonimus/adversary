#!/usr/bin/env node
// Fails when DEPENDENCIES.lock.md has drifted behind the latest release: if its
// "Last verified" date predates the most recent release tag, a release was cut without
// re-verifying the resolved-dependency snapshot, so a clone of that tag may not rebuild the
// bytes we tested. The lock is our anti-drift artifact; this keeps the artifact itself from
// drifting unnoticed. See slices/0080.
//
//   node scripts/lint-dep-lock-staleness.mjs [lockfile]   (default: DEPENDENCIES.lock.md)
//
// Zero dependencies by design, like the other scripts/*.mjs — drops into any checkout
// without an install step. Exit 1 if stale (or the lock cannot be dated).
//
// Needs tags present: in CI the checkout must fetch them (fetch-depth: 0), or there are no
// tags to compare against and the gate is a false green. The workflow owns that; this
// script only reports what git shows it.

import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';

// "Last verified: 2026-10-20 — native ...": the ISO date is the snapshot's as-of stamp.
const LOCK_DATE = /Last verified:\s*(\d{4}-\d{2}-\d{2})/;

/**
 * Pull the "Last verified" ISO date out of the lock text.
 * Throws when absent: a lock we cannot date is broken, and a broken lock must fail the gate
 * loudly, never pass by default.
 */
export function parseLockDate(text) {
  const match = LOCK_DATE.exec(text);
  if (!match) {
    throw new Error('no "Last verified: YYYY-MM-DD" line found in the lock file');
  }
  return match[1];
}

/**
 * Pure staleness decision. Dates are ISO YYYY-MM-DD strings, compared lexicographically —
 * for that fixed-width format string order is chronological order, so no Date/timezone
 * parsing is needed. No release tags (tagDate === null) is a non-stale pass: a repo with no
 * release cannot have a lock stale relative to a release. Same-day is a pass.
 */
export function evaluate({ lockDate, tagDate, tagName }) {
  if (tagDate === null) {
    return { stale: false, message: `no release tags to compare against; lock dated ${lockDate}.` };
  }
  if (lockDate < tagDate) {
    return {
      stale: true,
      message:
        `dependency lock is stale: "Last verified" ${lockDate} predates release tag ` +
        `${tagName} (${tagDate}). Re-resolve the envs, re-run the native suite, and ` +
        're-stamp DEPENDENCIES.lock.md in the same commit (see its "Bump" section).',
    };
  }
  return { stale: false, message: `lock ${lockDate} is current with latest release ${tagName} (${tagDate}).` };
}

/** Latest release tag (vX.Y.Z…) by creation date, or null when none exist. */
function latestReleaseTag() {
  const out = execFileSync(
    'git',
    ['for-each-ref', '--sort=-creatordate', '--format=%(creatordate:short)\t%(refname:short)', 'refs/tags/v*'],
    { encoding: 'utf8' },
  ).trim();
  if (out === '') return { tagDate: null, tagName: null };
  const [tagDate, tagName] = out.split('\n')[0].split('\t');
  return { tagDate, tagName };
}

function main(argv) {
  const lockPath = argv[0] ?? 'DEPENDENCIES.lock.md';
  const lockDate = parseLockDate(readFileSync(lockPath, 'utf8'));
  const { tagDate, tagName } = latestReleaseTag();
  const { stale, message } = evaluate({ lockDate, tagDate, tagName });
  if (stale) {
    console.error(`lint-dep-lock-staleness: ${message}`);
    return 1;
  }
  console.log(`lint-dep-lock-staleness: ${message}`);
  return 0;
}

// Run only as a CLI, not when imported by the test.
if (import.meta.url === `file://${process.argv[1]}`) {
  process.exit(main(process.argv.slice(2)));
}
