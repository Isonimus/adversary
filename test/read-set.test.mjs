// Drift guard (slices/0038): the pre-commit hook archives a fixed list of paths from the
// staged tree, and lint-docs.mjs reads a set of paths it exports as READ_SCOPE. If the
// linter ever reads outside what the hook archives, the hook grades a commit against a file
// it never extracted — green hook, red HEAD. This test holds the two lists equal so that
// the next divergence fails here instead of going quiet. See scripts/lint-docs.mjs and
// .claude/hooks/pre-commit, which both cite this file.
//
// Run: node --test test/read-set.test.mjs

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { READ_SCOPE } from '../scripts/lint-docs.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');

/** The hook's archive list — the `for candidate in … ; do` pathspec it materialises. */
function hookArchiveList() {
  const hook = readFileSync(join(repoRoot, '.claude/hooks/pre-commit'), 'utf8');
  const match = hook.match(/for candidate in (.+?);\s*do/);
  // A guard that cannot find its input must fail, not vacuously pass: a renamed or
  // restructured loop would otherwise silently disable this check.
  assert.ok(match, 'could not locate the `for candidate in …; do` archive list in the pre-commit hook');
  return match[1].trim().split(/\s+/);
}

test('hook archive list and linter READ_SCOPE are the same set', () => {
  const hookList = new Set(hookArchiveList());
  const scope = new Set(READ_SCOPE);

  const onlyInHook = [...hookList].filter((p) => !scope.has(p));
  const onlyInScope = [...scope].filter((p) => !hookList.has(p));

  assert.deepEqual(
    { onlyInHook, onlyInScope },
    { onlyInHook: [], onlyInScope: [] },
    'hook archive list and lint-docs READ_SCOPE have drifted — a path read by the linter ' +
      'but not archived by the hook (or vice versa) checks a commit against a file it never extracted',
  );
});

test('both lists are non-empty (the regex actually captured paths)', () => {
  assert.ok(hookArchiveList().length > 0, 'hook archive list parsed as empty');
  assert.ok(READ_SCOPE.length > 0, 'READ_SCOPE is empty');
});
