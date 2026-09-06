#!/usr/bin/env node
// Checks that immutable documents stayed immutable (ADR-0019).
//
//   node scripts/check-immutable.mjs <base> <head>
//
// Compares two tree-ish revisions. Fails when a document under adr/ or slices/ that exists
// in <base> has had its body changed other than by appending, or has been removed.
//
// This is a sibling of `build-index --check`, not a lint rule, because it needs git — and
// lint(root) is a pure function over one directory, which is what makes it testable and
// reusable from init-method (ADR-0019). Frontmatter is deliberately out of scope: status
// and supersession fields are the mutable surface by design (ADR-0002).

import { execFileSync } from 'node:child_process';
import { realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { parseFrontmatter } from './lint-docs.mjs';

const DOC_DIRS = ['adr', 'slices'];

/** The generated index lives among the documents but is not one (ADR-0010). */
const GENERATED = 'INDEX.md';

const git = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });

/** Immutable documents in a tree. A pathspec matching nothing is not an error for
 *  ls-tree, so a repo without slices/ needs no special case. */
function docsIn(rev) {
  const out = git('ls-tree', '-r', '--name-only', rev, '--', ...DOC_DIRS);
  return out.split('\n').filter((p) => p.endsWith('.md') && !p.endsWith(`/${GENERATED}`));
}

/**
 * The document's body as lines, with trailing blanks dropped so that a file gaining or
 * losing a final newline does not read as an edit.
 *
 * Returns null when the frontmatter does not parse: the body boundary is then unknown, and
 * comparing whole files instead would silently change what this check means. Rule 1 already
 * errors on those files, so nothing escapes between the two (ADR-0019).
 */
function bodyLines(text) {
  const parsed = parseFrontmatter(text);
  if (!parsed.ok) return null;
  const lines = parsed.body.split('\n');
  while (lines.length && lines[lines.length - 1].trim() === '') lines.pop();
  return lines;
}

const show = (rev, path) => git('show', `${rev}:${path}`);

/**
 * Index of the first line of `was` that is not still present, in order, in `now` — or -1
 * when the older body survives intact.
 *
 * A subsequence test rather than a prefix test: insertions anywhere are legitimate here,
 * because this repo's sanctioned way to correct a wrong claim is a marker placed *at* the
 * claim (ADR-0001's 2026-07-21 amendment, used in ADR-0003), not a note at the end. What
 * it forbids is a line disappearing or changing, which is what "immutable" means.
 *
 * The known limit: deleting a line that recurs verbatim later in the same body reads as
 * intact. Blank lines and bare headings are the realistic instances, and both are prose
 * scaffolding rather than claims (ADR-0019).
 */
function firstLostLine(was, now) {
  let cursor = 0;
  for (let i = 0; i < was.length; i++) {
    while (cursor < now.length && now[cursor] !== was[i]) cursor++;
    if (cursor === now.length) return i;
    cursor++;
  }
  return -1;
}

/** Whether a revision resolves. Used only to tell an unborn branch from a real base —
 *  every other git failure here is a genuine fault and is left to throw. */
function revExists(rev) {
  try {
    git('rev-parse', '--verify', '--quiet', `${rev}^{commit}`);
    return true;
  } catch {
    return false;
  }
}

/**
 * @returns {string[]} one message per violation; empty means the bodies only grew.
 */
export function checkImmutable(base, head) {
  const problems = [];
  const present = new Set(docsIn(head));

  for (const path of docsIn(base)) {
    if (!present.has(path)) {
      problems.push(`${path}: removed. Immutable documents are never deleted — supersede it instead (ADR-0010).`);
      continue;
    }

    const was = bodyLines(show(base, path));
    const now = bodyLines(show(head, path));
    if (was === null || now === null) continue; // rule 1 owns unparseable frontmatter

    const lost = firstLostLine(was, now);
    if (lost !== -1) {
      problems.push(
        `${path}: body line ${lost + 1} was deleted or rewritten. Immutable prose may gain lines,\n` +
        `             never lose or change them (ADR-0019).\n` +
        `    was: ${was[lost]}`,
      );
    }
  }
  return problems;
}

/**
 * Defaults are what let the pre-commit *framework* call this with no arguments: its `entry`
 * is argv, never a shell, so `$(git write-tree)` in a config would be passed literally. The
 * hook still names both revisions explicitly, having already computed the tree (ADR-0018).
 */
function main(argv) {
  const [base = 'HEAD', head] = argv;

  if (base === 'HEAD' && !revExists('HEAD')) {
    console.log('check-immutable: no commits yet — nothing to have edited.');
    return 0;
  }

  const problems = checkImmutable(base, head ?? git('write-tree').trim());
  for (const problem of problems) console.error(`  IMMUTABLE  ${problem}`);
  if (problems.length) {
    console.error(
      `\n${problems.length} immutable document(s) changed below the frontmatter.\n` +
      'Record the change as an appended `## Amendment — <date>: …` block, or as a new ADR that\n' +
      'supersedes this one and says why the old reasoning was wrong (ADR-0010).',
    );
    return 1;
  }
  return 0;
}

// Resolved on both sides before comparing: the hook invokes this through the vendored copy
// and a raw `file://${argv[1]}` compare is false whenever a symlink is in the path, which
// silently no-ops main() (ADR-0015 amendment).
if (process.argv[1] && realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main(process.argv.slice(2)));
}
