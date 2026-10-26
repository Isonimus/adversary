#!/usr/bin/env node
// Verifies the third-party patched build ZIPs pinned by URL in platformio.ini against the
// SHA256 hashes recorded in scripts/build-zip-checksums.json, so a swapped upstream release
// asset (the URL does not pin the bytes) cannot enter a build unnoticed. See ADR-0125.
//
//   node scripts/lint-build-zip-integrity.mjs [platformio.ini]   (default: platformio.ini)
//
// Zero dependencies by design, like the other scripts/*.mjs. Exit 1 on any failure.
//
// Two stages, cheap one first:
//   1. Set cross-check (network-free): every GitHub release-asset archive URL in
//      platformio.ini must have a checksum entry (an unverified pin fails, so a bump cannot
//      bypass the gate), and every checksum entry must still be referenced (a stale entry
//      fails, so dead pins are pruned).
//   2. Hash compare: download each URL, stream it through SHA256, compare to the recorded
//      hash. A mismatch fails loud; a failed download retries, then fails (never a
//      pass-by-default on a network error).

import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const CHECKSUMS_PATH = 'scripts/build-zip-checksums.json';
// A pinned third-party build input: a GitHub release-asset archive. Registry lib_deps are
// owner/name@version, not URLs, so they never match here (they stay covered by the lock).
const RELEASE_ARCHIVE_URL = /https:\/\/github\.com\/[^\s"']+?\.(?:zip|tar\.gz|tar\.xz)/g;
const SHA256_VALUE = /^sha256:[0-9a-f]{64}$/;
const MAX_DOWNLOAD_ATTEMPTS = 3;

/**
 * Extract the pinned release-asset archive URLs from platformio.ini text, deduped and sorted.
 * Exported for the test.
 */
export function parsePinnedUrls(iniText) {
  return [...new Set(iniText.match(RELEASE_ARCHIVE_URL) ?? [])].sort();
}

/**
 * Parse build-zip-checksums.json into a Map url -> "sha256:<hex>".
 * Throws on a malformed file or a malformed hash value: a checksum file we cannot trust must
 * fail the gate loudly, never pass by default. Exported for the test.
 */
export function parseChecksums(jsonText) {
  const parsed = JSON.parse(jsonText);
  const table = parsed.checksums;
  if (table === null || typeof table !== 'object' || Array.isArray(table)) {
    throw new Error('checksums file has no "checksums" object');
  }
  const map = new Map();
  for (const [url, value] of Object.entries(table)) {
    if (typeof value !== 'string' || !SHA256_VALUE.test(value)) {
      throw new Error(`checksum for ${url} is not a "sha256:<64 hex>" value: ${value}`);
    }
    map.set(url, value);
  }
  return map;
}

/**
 * Network-free set cross-check. Returns { missing, stale }, both empty means proceed to
 * hashing. missing = pinned URLs with no checksum entry; stale = checksum entries no longer
 * pinned. Exported for the test.
 */
export function crossCheck(iniUrls, checksumUrls) {
  const checksumSet = new Set(checksumUrls);
  const iniSet = new Set(iniUrls);
  return {
    missing: iniUrls.filter((url) => !checksumSet.has(url)),
    stale: checksumUrls.filter((url) => !iniSet.has(url)),
  };
}

/**
 * Compare recorded hashes against freshly computed ones. Returns an array of
 * { url, expected, actual } for every differing URL. Exported for the test.
 */
export function compareHashes(checksums, actualHashes) {
  const mismatches = [];
  for (const [url, expected] of checksums) {
    const actual = actualHashes.get(url);
    if (actual !== expected) mismatches.push({ url, expected, actual: actual ?? '(not fetched)' });
  }
  return mismatches;
}

/** Download a URL and return its "sha256:<hex>", streamed so a 549 MB asset is not buffered. */
async function fetchSha256(url) {
  let lastError;
  for (let attempt = 1; attempt <= MAX_DOWNLOAD_ATTEMPTS; attempt++) {
    try {
      const res = await fetch(url, { redirect: 'follow' });
      if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
      const hash = createHash('sha256');
      for await (const chunk of res.body) hash.update(chunk);
      return `sha256:${hash.digest('hex')}`;
    } catch (err) {
      lastError = err;
      if (attempt < MAX_DOWNLOAD_ATTEMPTS) {
        console.error(`  fetch attempt ${attempt} for ${url} failed (${err.message}); retrying`);
      }
    }
  }
  throw new Error(`could not fetch ${url} after ${MAX_DOWNLOAD_ATTEMPTS} attempts: ${lastError.message}`);
}

async function main(argv) {
  const iniPath = argv[0] ?? 'platformio.ini';
  const iniUrls = parsePinnedUrls(readFileSync(iniPath, 'utf8'));
  const checksums = parseChecksums(readFileSync(CHECKSUMS_PATH, 'utf8'));

  const { missing, stale } = crossCheck(iniUrls, [...checksums.keys()]);
  if (missing.length > 0 || stale.length > 0) {
    for (const url of missing) {
      console.error(`lint-build-zip-integrity: pinned URL has no checksum entry: ${url}`);
    }
    for (const url of stale) {
      console.error(`lint-build-zip-integrity: checksum entry is no longer pinned (remove it): ${url}`);
    }
    console.error(`\nUpdate ${CHECKSUMS_PATH} so it matches the pinned URLs in ${iniPath} (ADR-0125).`);
    return 1;
  }

  const actualHashes = new Map();
  for (const url of iniUrls) {
    console.log(`fetching + hashing ${url}`);
    actualHashes.set(url, await fetchSha256(url));
  }

  const mismatches = compareHashes(checksums, actualHashes);
  if (mismatches.length > 0) {
    for (const { url, expected, actual } of mismatches) {
      console.error(`lint-build-zip-integrity: SHA256 MISMATCH for ${url}`);
      console.error(`  expected ${expected}`);
      console.error(`  actual   ${actual}`);
    }
    console.error('\nThe bytes at a pinned URL changed. Investigate before trusting the build (ADR-0125).');
    return 1;
  }

  console.log(`lint-build-zip-integrity: ${iniUrls.length} pinned ZIP(s) match their recorded SHA256.`);
  return 0;
}

// Run only as a CLI, not when imported by the test.
if (import.meta.url === `file://${process.argv[1]}`) {
  main(process.argv.slice(2)).then((code) => process.exit(code)).catch((err) => {
    console.error(`lint-build-zip-integrity: ${err.message}`);
    process.exit(1);
  });
}
