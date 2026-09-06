#!/usr/bin/env node
// Checks the document invariants defined in ADR-0003.
//
// Zero dependencies by design: this drops into any repo regardless of package manager.
// The frontmatter schema (ADR-0002) is a closed seven-field shape, small enough to parse
// by hand and not worth a YAML dependency.
//
//   node scripts/lint-docs.mjs [repo-root ...]     (default: cwd)
//   --quiet   only print problems
//
// Exit 1 if any error-severity rule fails. Warnings never fail the build.

import { readFileSync, readdirSync, existsSync, realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join, basename, dirname, relative } from 'node:path';

const STATUSES = ['accepted', 'proposed', 'superseded', 'amended'];
const TYPES = ['architecture', 'slice', 'batch'];
const REQUIRED = ['id', 'title', 'type', 'status', 'date'];
const DOC_DIRS = ['adr', 'slices'];

// Prose that is read as instruction rather than as a record (ADR-0020). `CLAUDE.md` is
// loaded at the start of every session and the command files are the assistant's own
// procedures, so a citation rotting in either misroutes work silently — the corpus itself
// stays green because rules 8 and 9 never open these files. Directories are scanned one
// level deep; anything else here is a plain file path.
const PROSE_FILES = ['CLAUDE.md', 'README.md'];
const PROSE_DIRS = ['docs', '.claude/commands'];

/**
 * Every path this linter reads, relative to the repo root — the hook's archive list
 * (ADR-0018) in machine-readable form.
 *
 * The hook materialises the staged tree and copies only what the checks read, so a rule
 * reading outside this set runs against a file that was never extracted. That happened:
 * rules 14 and 15 shipped reading four paths the hook did not copy, and were dead there
 * for a release while passing in CI. `test/read-set.test.mjs` holds the two lists equal.
 */
export const READ_SCOPE = [
  ...DOC_DIRS, 'LEDGER.md', ...PROSE_FILES, ...PROSE_DIRS, 'scripts', 'package.json',
];

/** Whether a root-relative path lies inside the checked scope. Outside it, the hook and a
 *  working-tree run would disagree, and a check that depends on where it runs is worse
 *  than no check. */
function inReadScope(rootRelative) {
  if (rootRelative.startsWith('..')) return false;
  return READ_SCOPE.some((entry) => rootRelative === entry || rootRelative.startsWith(`${entry}/`));
}

// The date the required-slice-section rules (R12/R13) shipped (ADR-0004, ADR-0011). A
// slice dated before this predates the rules and only warns; one dated on or after must
// comply. Without the split, a repo adopting this linter would go red on its whole legacy
// corpus (boxel's ~104 slices, gamatar's) the day it installs — but a rule that only ever
// warns never enforces the section on new work either. The date gates legacy in without
// letting new slices skip the contract.
const SLICE_SECTIONS_SINCE = '2026-07-22';

// --- frontmatter ------------------------------------------------------------

/** Ids are always 4-digit strings. Normalising early sidesteps YAML's octal reading of
 *  bare 0112 and makes ids safe as object keys. */
const normId = (v) => String(v).trim().padStart(4, '0');

const isId = (v) => /^\d{1,4}$/.test(String(v).trim());

/**
 * ISO 8601 `YYYY-MM-DD`, and a day that exists — `2026-02-31` parses but round-trips wrong.
 *
 * `date` is not decoration: R12/R13 pick their severity by string-comparing it against
 * SLICE_SECTIONS_SINCE, so anything sorting above `2026-07-22` grades as current and
 * anything below grades as legacy. Unvalidated, `date: sometime last tuesday` graded as
 * *current* purely because 's' > '2', and a copy-pasted earlier date graded as legacy —
 * shipping a slice with no Definition of Done on a green build. ADR-0002 already specifies
 * ISO 8601; this is that specification made executable.
 */
function isCalendarDate(v) {
  const text = String(v).trim();
  if (!/^\d{4}-\d{2}-\d{2}$/.test(text)) return false;
  // `2026-02-30` parses and normalises to March, so the round-trip catches it. `2026-00-01`
  // does not parse at all, and `toISOString()` on an invalid Date throws — checked first,
  // because a linter that dies with a stack trace on a malformed date reports nothing about
  // the other documents and breaks `--update`'s report (ADR-0021).
  const parsed = new Date(`${text}T00:00:00Z`);
  if (Number.isNaN(parsed.getTime())) return false;
  return parsed.toISOString().startsWith(text);
}

// Citations, bare or qualified (ADR-0009). A leading `<repo>:` says the decision lives in
// another repo's corpus, which this linter cannot open and so must skip. The colon has to
// be adjacent, leaving an ordinary sentence ending in a colon ("see also: ADR-0004")
// resolving locally as before.
const CITATION = /(?:([A-Za-z][\w.-]*):)?ADR[-\s](\d{1,4})/g;

/**
 * `text` with link destinations and URLs removed, so only prose is scanned for citations.
 *
 * A URL path can contain an `ADR-1234`-shaped run that cites nothing —
 * `https://example.com/docs/ADR-9999`, or a ticket link. Rules 8 and 14 are error severity,
 * so one coincidence blocks a correct commit, and the advice their message gives is
 * unusable: the `<repo>:` qualifier cannot be written inside a URL. Link *text* is kept,
 * because `[ADR-0020](adr/0020-….md)` is a citation and rule 15 checks the target
 * separately.
 */
const citableText = (text) =>
  text.replace(/\]\([^)]*\)/g, ']()').replace(/\S*:\/\/\S*/g, '');

/**
 * Ids cited in `text` that this repo is expected to own — cross-repo refs skipped.
 *
 * A qualifier naming *this* repo resolves locally (ADR-0020). Text that is vendored into
 * other repos must qualify its citations, or a bare `ADR-0005` copied into gamatar reads
 * as gamatar's ADR-0005; but qualifying it would also put it permanently beyond checking,
 * since ADR-0009 skips every qualified reference. Recognising our own name is what keeps
 * `stele:ADR-0005` verified in the one corpus that can verify it.
 */
function* localCitations(text, selfRepo = null) {
  for (const [, repo, id] of citableText(text).matchAll(CITATION)) {
    if (repo === undefined || (selfRepo !== null && repo === selfRepo)) yield normId(id);
  }
}

/** This repo's own name for citation purposes: the unscoped half of package.json `name`.
 *  Null when there is no readable name — rule 11 owns malformed package.json, and without
 *  a name there is simply no self-qualifier to recognise. */
function repoName(root) {
  const path = join(root, 'package.json');
  if (!existsSync(path)) return null;
  let pkg;
  try {
    pkg = JSON.parse(readFileSync(path, 'utf8'));
  } catch {
    return null;
  }
  const name = typeof pkg.name === 'string' ? pkg.name : '';
  const unscoped = name.startsWith('@') ? name.slice(name.indexOf('/') + 1) : name;
  return unscoped === '' ? null : unscoped;
}

// Inline markdown links, with the optional title form `[x](path "title")`.
const LINK = /\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)/g;

/** Link targets in `line` that name a file in this repo. External URLs, mail links,
 *  in-page anchors and absolute paths are all outside what a file check can decide. */
function* relativeLinks(line) {
  for (const [, target] of line.matchAll(LINK)) {
    if (/^(?:[a-z][a-z0-9+.-]*:|\/\/|\/|#)/i.test(target)) continue;
    const path = target.split('#')[0].split('?')[0];
    if (path !== '') yield path;
  }
}

/**
 * Parses a flat scalar: quoted string, inline list, or bare value.
 *
 * A double-quoted value is decoded as a JSON string, because that is how the migrator
 * emits free-text titles (`"Status effects: \"poison\"…"`). The two must share one
 * escaping convention or a title with an inner quote round-trips wrong: migrated clean,
 * misread at lint. Single-quoted values (ids) carry no escapes and only shed their quotes.
 */
function parseScalar(raw) {
  const v = raw.trim();
  if (v.startsWith('[') && v.endsWith(']')) {
    const inner = v.slice(1, -1).trim();
    if (!inner) return [];
    return inner.split(',').map((s) => parseScalar(s));
  }
  if (v.startsWith('"') && v.endsWith('"')) {
    try {
      return JSON.parse(v);
    } catch {
      return v.slice(1, -1);
    }
  }
  return v.replace(/^'|'$/g, '');
}

/**
 * Hand-rolled frontmatter reader for the ADR-0002 schema.
 * Supports `key: value`, inline lists `[a, b]`, and block lists (`-` items).
 * Returns { ok, data, body, error }.
 */
export function parseFrontmatter(text) {
  const lines = text.split('\n');
  if (lines[0].trim() !== '---') {
    return { ok: false, error: 'no frontmatter (file must open with ---)' };
  }
  const end = lines.indexOf('---', 1);
  if (end === -1) return { ok: false, error: 'frontmatter is not terminated by ---' };

  const data = {};
  let currentKey = null;

  for (let i = 1; i < end; i++) {
    const line = lines[i];
    if (!line.trim() || line.trim().startsWith('#')) continue;

    const blockItem = line.match(/^\s*-\s+(.*)$/);
    if (blockItem && currentKey) {
      if (!Array.isArray(data[currentKey])) data[currentKey] = [];
      data[currentKey].push(parseScalar(blockItem[1]));
      continue;
    }

    const kv = line.match(/^([A-Za-z_][\w-]*)\s*:\s*(.*)$/);
    if (!kv) return { ok: false, error: `unparseable frontmatter line ${i + 1}: "${line}"` };

    const [, key, rest] = kv;
    currentKey = key;
    data[key] = rest.trim() === '' ? [] : parseScalar(rest);
  }

  return { ok: true, data, body: lines.slice(end + 1).join('\n') };
}

// --- loading ----------------------------------------------------------------

export function loadDocs(root) {
  const dirs = DOC_DIRS.map((d) => join(root, d)).filter(existsSync);
  const docs = [];

  for (const dir of dirs) {
    for (const file of readdirSync(dir).sort()) {
      if (!file.endsWith('.md') || file === 'INDEX.md') continue;
      const path = join(dir, file);
      const parsed = parseFrontmatter(readFileSync(path, 'utf8'));
      docs.push({ path, file, kind: basename(dir), ...parsed });
    }
  }
  return docs;
}

/** The prose files present under `root`, as { path, text }. Missing ones are simply
 *  absent — not every repo has docs/ or slash commands. */
function loadProse(root) {
  const paths = PROSE_FILES.map((f) => join(root, f));
  for (const dir of PROSE_DIRS) {
    const full = join(root, dir);
    if (!existsSync(full)) continue;
    paths.push(...readdirSync(full).sort().filter((f) => f.endsWith('.md')).map((f) => join(full, f)));
  }
  return paths.filter(existsSync).map((path) => ({ path, text: readFileSync(path, 'utf8') }));
}

// --- section helpers --------------------------------------------------------
// Slice rules (R12/R13) assert the presence and shape of `## Sections` in the body prose.
// This is the only place the linter reads body text structurally; ADR-0002 keeps
// frontmatter the machine-readable surface, and a markdown heading is not frontmatter.

/**
 * Body lines with fenced code blocks blanked out, positions preserved.
 *
 * A `## Verification` inside a fence is a *quotation* of the rule, not compliance with it.
 * Reproduced 2026-07-27: a slice whose only two required sections sat in a ```markdown
 * sample — exactly what a document explaining the slice template contains — lints clean,
 * which is a false green on the two rules that define "done".
 */
function withoutFences(body) {
  const fence = /^\s*(```|~~~)/;
  let open = null;
  return (body ?? '').split('\n').map((line) => {
    const marker = line.match(fence);
    if (marker && open === null) {
      open = marker[1];
      return '';
    }
    if (marker && line.trim().startsWith(open)) {
      open = null;
      return '';
    }
    return open === null ? line : '';
  });
}

/** The text under a `## Heading`, up to the next `#`/`##` heading or end of body.
 *  Returns null when the heading is absent — distinct from a present-but-empty section. */
function sectionText(body, name) {
  const heading = new RegExp(`^##\\s+${name}\\s*$`, 'i');
  const lines = withoutFences(body);
  const start = lines.findIndex((l) => heading.test(l.trim()));
  if (start === -1) return null;
  const rest = lines.slice(start + 1);
  const end = rest.findIndex((l) => /^#{1,2}\s/.test(l.trim()));
  return (end === -1 ? rest : rest.slice(0, end)).join('\n');
}

/** Which Gherkin step keywords appear as line-leading steps, tolerant of a leading list
 *  marker and markdown emphasis (`- **Given** …`). Shape only (ADR-0011): it reads that a
 *  step exists, never what the step claims. */
function gherkinSteps(text) {
  const kinds = new Set();
  for (const raw of text.split('\n')) {
    const line = raw.trim().replace(/^[-*+>]\s*/, '').replace(/[*_`]/g, '');
    const m = line.match(/^(?:and\s+|but\s+)?(given|when|then)\b/i);
    if (m) kinds.add(m[1].toLowerCase());
  }
  return kinds;
}

/** A `## Definition of Done` has a real scenario when all three step kinds are present —
 *  a complete Given/When/Then triad's worth of steps, in any order. */
function hasGherkinTriad(text) {
  const steps = gherkinSteps(text);
  return steps.has('given') && steps.has('when') && steps.has('then');
}

// --- rules ------------------------------------------------------------------
// Each rule is (docs, root, report) => void. `report` takes (severity, path, message).
// Every rule traces to an observed failure; see the table in ADR-0003.

const rules = {
  // R10 — a linter that finds nothing must not report success. Pointed at `boxel/adr`
  // rather than the repo root, this printed "0 document(s) — ok" and exited 0: a hook
  // wired to a wrong path would go green forever while checking nothing, which is the
  // exact failure mode this file exists to prevent.
  //
  // Severity splits on *why* the corpus is empty. No adr/ or slices/ at all means the
  // root is wrong — no repo using this method lacks both, so that is an error. Dirs that
  // exist but hold no documents are a correctly-scaffolded repo that has not written its
  // first ADR yet; erroring there would fail `npm run lint` during install, so it warns.
  corpus(docs, root, report) {
    if (docs.length > 0) return;
    const present = DOC_DIRS.filter((d) => existsSync(join(root, d)));
    if (present.length === 0) {
      report('error', root, `R10 no ${DOC_DIRS.join('/ or ')}/ directory here — is this the repo root?`);
    } else {
      report('warn', root, `R10 ${present.map((d) => `${d}/`).join(' and ')} present but empty — no documents to check`);
    }
  },

  // R1 — frontmatter present, parseable, required fields non-empty.
  frontmatter(docs, _root, report) {
    for (const d of docs) {
      if (!d.ok) {
        report('error', d.path, `R1 ${d.error}`);
        continue;
      }
      for (const field of REQUIRED) {
        const v = d.data[field];
        if (v === undefined || v === '' || (Array.isArray(v) && v.length === 0)) {
          report('error', d.path, `R1 missing required field "${field}"`);
        }
      }
      if (d.data.date !== undefined && !isCalendarDate(d.data.date)) {
        report('error', d.path, `R1 date "${d.data.date}" is not a calendar date in YYYY-MM-DD form`);
      }
    }
  },

  // R2 — id matches the filename ordinal; no duplicates. Slug is not a key:
  // 0068-lava-fluid and 0128-lava-fluid share one, as do the three farming files.
  ids(docs, _root, report) {
    const seen = new Map();
    for (const d of docs) {
      if (!d.ok || d.data.id === undefined) continue;
      const id = normId(d.data.id);

      if (!isId(d.data.id)) {
        report('error', d.path, `R2 id "${d.data.id}" is not a number`);
        continue;
      }
      const fromName = d.file.match(/^(\d{1,4})/);
      if (!fromName) {
        report('error', d.path, `R2 filename does not start with an ordinal`);
      } else if (normId(fromName[1]) !== id) {
        report('error', d.path, `R2 id ${id} does not match filename ordinal ${normId(fromName[1])}`);
      }

      if (seen.has(id)) {
        report('error', d.path, `R2 duplicate id ${id} (also in ${basename(seen.get(id))})`);
      } else {
        seen.set(id, d.path);
      }
    }
  },

  // R3 — closed vocabularies. Legacy carried Implemented/ACCEPTED/**Accepted**.
  vocabulary(docs, _root, report) {
    for (const d of docs) {
      if (!d.ok) continue;
      const { status, type } = d.data;
      if (status !== undefined && !STATUSES.includes(status)) {
        report('error', d.path, `R3 status "${status}" not in [${STATUSES.join(', ')}]`);
      }
      if (type !== undefined && !TYPES.includes(type)) {
        report('error', d.path, `R3 type "${type}" not in [${TYPES.join(', ')}]`);
      }
    }
  },

  // R4/R5/R6/R7 — the supersession graph. These share an index, so they run together.
  supersession(docs, _root, report) {
    const byId = new Map();
    for (const d of docs) {
      if (d.ok && d.data.id !== undefined && isId(d.data.id)) byId.set(normId(d.data.id), d);
    }
    const listOf = (d, key) => {
      const v = d.data[key];
      if (v === undefined) return [];
      return (Array.isArray(v) ? v : [v]).filter((x) => x !== '').map(normId);
    };

    for (const [id, d] of byId) {
      const supersededBy = listOf(d, 'superseded_by');
      const supersedes = listOf(d, 'supersedes');

      // R4 — a document cannot supersede itself. Self-reference satisfies every other
      // check in this rule vacuously: the bidirectionality test finds the id in its own
      // list, R6 sees a superseded status with a non-empty superseded_by, and R7 sees a
      // target that is not "accepted". The whole graph agrees, about nothing.
      if (supersedes.includes(id) || supersededBy.includes(id)) {
        report('error', d.path, `R4 ADR ${id} supersedes itself — a decision is replaced by a later one, never by itself`);
      }

      // R5 — dangling references. Legacy 0051 and 0061 claimed supersession with no
      // resolvable target at all.
      for (const key of ['supersedes', 'superseded_by']) {
        for (const ref of listOf(d, key)) {
          if (!byId.has(ref)) report('error', d.path, `R5 ${key} references ADR ${ref}, which does not exist`);
        }
      }

      // R4 — bidirectionality. This is the 0112/0113/0114 -> 0122 defect: each declared
      // itself superseded, and 0122 acknowledged none of them.
      for (const ref of supersededBy) {
        const target = byId.get(ref);
        if (target && !listOf(target, 'supersedes').includes(id)) {
          report('error', d.path, `R4 declares superseded_by ${ref}, but ADR ${ref} does not list ${id} in supersedes`);
        }
      }
      for (const ref of supersedes) {
        const target = byId.get(ref);
        if (target && !listOf(target, 'superseded_by').includes(id)) {
          report('error', d.path, `R4 declares it supersedes ${ref}, but ADR ${ref} does not list ${id} in superseded_by`);
        }
      }

      // R6 — status and supersession must agree. Legacy 0112/0114 read
      // "Status: Accepted" four lines above "Superseded by ADR 0122".
      const isSuperseded = d.data.status === 'superseded';
      if (isSuperseded && supersededBy.length === 0) {
        report('error', d.path, `R6 status is superseded but superseded_by is empty`);
      }
      if (!isSuperseded && supersededBy.length > 0) {
        report('error', d.path, `R6 superseded_by names ${supersededBy.join(', ')} but status is "${d.data.status}"`);
      }
    }

    // R7 — the same disagreement seen from the other side: B claims to supersede A while
    // A still reads as live. R6 cannot catch this when A says nothing at all.
    for (const [id, d] of byId) {
      for (const ref of listOf(d, 'supersedes')) {
        const target = byId.get(ref);
        if (target && target.data.status === 'accepted') {
          report('error', target.path, `R7 is "accepted" but ADR ${id} claims to supersede it`);
        }
      }
    }
  },

  // R8 — ledger citations resolve. The ledger is the only mutable file (ADR-0001);
  // if it cites a decision, that decision must exist.
  ledger(docs, root, report) {
    const path = join(root, 'LEDGER.md');
    if (!existsSync(path)) return;
    const ids = new Set(docs.filter((d) => d.ok && d.data.id !== undefined).map((d) => normId(d.data.id)));

    const text = readFileSync(path, 'utf8');
    const self = repoName(root);
    text.split('\n').forEach((line, i) => {
      for (const id of localCitations(line, self)) {
        if (!ids.has(id)) {
          report('error', path, `R8 line ${i + 1} cites ADR ${id}, which does not exist. Another repo's decision is cited as \`<repo>:ADR-${id}\` (ADR-0009).`);
        }
      }
    });
  },

  // R14/R15 — the prose that is read as instruction (ADR-0020). Rules 8 and 9 open
  // `LEDGER.md` and the corpus and nothing else, so `CLAUDE.md`, `README.md`, `docs/` and
  // the slash commands were never checked at all. That is not hypothetical: gamatar's
  // vendored `/remember` said "the exact failure ADR-0005 exists to prevent", and
  // gamatar's ADR-0005 is a superseded decision about canvas face textures.
  //
  // Error, not a warning like R9: measured across boxel's prose (43 bare references) and
  // gamatar's, every reference that is meant to be local resolves, so the severity that
  // forced R9 to warn — legacy volume — is absent here.
  //
  // The two run together because they share the file set. R15 checks relative link
  // targets, and only here: a broken link inside an immutable document cannot be fixed
  // without the rewrite ADR-0019 forbids, so it is not something to fail a build on.
  prose(docs, root, report) {
    const ids = new Set(docs.filter((d) => d.ok && d.data.id !== undefined).map((d) => normId(d.data.id)));
    const self = repoName(root);

    for (const { path, text } of loadProse(root)) {
      text.split('\n').forEach((line, i) => {
        for (const id of localCitations(line, self)) {
          if (!ids.has(id)) {
            report('error', path, `R14 line ${i + 1} cites ADR ${id}, which does not exist. Another repo's decision is cited as \`<repo>:ADR-${id}\` (ADR-0009).`);
          }
        }
        for (const target of relativeLinks(line)) {
          const resolved = relative(root, join(dirname(path), target));
          if (!inReadScope(resolved)) continue;
          if (!existsSync(join(root, resolved))) {
            report('error', path, `R15 line ${i + 1} links to ${target}, which does not exist`);
          }
        }
      });
    }
  },

  // R11 — every verify script is wired into package.json (ADR-0004). The harness's
  // load-bearing half: an unwired `*-verify.mjs` ran once on the day it was written and
  // never again — ADR-0004 Finding 2 found eleven of twelve boxel scripts in exactly that
  // state. This is the first *harness* rule; R1–R9 (and R10) check documents. It reads
  // scripts/ and package.json, never CLAUDE.md, so no prose enters the checked surface.
  //
  // Probes are excluded by name: a probe answers a design question once and its number
  // goes in an ADR, so it is not a standing regression and is not required to be wired.
  harnessWiring(_docs, root, report) {
    const scriptsDir = join(root, 'scripts');
    const pkgPath = join(root, 'package.json');
    if (!existsSync(scriptsDir) || !existsSync(pkgPath)) return;

    let pkg;
    try {
      pkg = JSON.parse(readFileSync(pkgPath, 'utf8'));
    } catch (err) {
      report('error', pkgPath, `R11 package.json is not valid JSON: ${err.message}`);
      return;
    }

    // A script is wired if its filename is the basename of a token in any npm command.
    // Tokenising and comparing basenames — rather than a substring test — is what keeps
    // `a-verify.mjs` from matching a runner that only mentions `xa-verify.mjs`, and lets
    // one aggregate command (`node scripts/a.mjs && node scripts/b.mjs`) wire both.
    const wired = new Set(
      Object.values(pkg.scripts ?? {})
        .flatMap((cmd) => String(cmd).split(/[\s'"]+/))
        .map((tok) => basename(tok)),
    );

    for (const file of readdirSync(scriptsDir).sort()) {
      if (!/-verify\.(mjs|mts)$/.test(file)) continue;
      if (!wired.has(file)) {
        report('error', join(scriptsDir, file), `R11 ${file} is not wired into package.json — it would run never`);
      }
    }
  },

  // R12/R13 — required slice sections. A slice is a feature work-unit (ADR-0001); two
  // sections complete its contract: `## Verification` names the proof (ADR-0004), and
  // `## Definition of Done` states the acceptance criteria in Given/When/Then (ADR-0011).
  // Both requirements predated any check — ADR-0004 named the Verification section but
  // nothing enforced it, the same gap R11 closed for wiring. They are checked together
  // because they share one severity rule.
  //
  // Severity splits on the slice's own date (SLICE_SECTIONS_SINCE): a slice predating the
  // rules only warns, so a repo's legacy corpus does not go red on adoption; one dated on
  // or after must comply, so new work is enforced rather than merely nagged. Presence and
  // triad-shape are all this checks — whether a scenario is correct or the set complete is
  // the coverage question, left to `/wrap-up` (ADR-0004, ADR-0011).
  sliceSections(docs, _root, report) {
    for (const d of docs) {
      if (!d.ok || d.data.type !== 'slice') continue;
      const severity = String(d.data.date ?? '') >= SLICE_SECTIONS_SINCE ? 'error' : 'warn';

      if (sectionText(d.body, 'Verification') === null) {
        report(severity, d.path, `R12 type: slice has no "## Verification" section (ADR-0004)`);
      }

      const dod = sectionText(d.body, 'Definition of Done');
      if (dod === null) {
        report(severity, d.path, `R13 type: slice has no "## Definition of Done" section (ADR-0011)`);
      } else if (!hasGherkinTriad(dod)) {
        report(severity, d.path, `R13 "## Definition of Done" has no Given/When/Then scenario (ADR-0011)`);
      }
    }
  },

  // R9 — prose cross-references. Warning only, deliberately: boxel carries 567 bare
  // references, some pointing at external or historical context. Failing the build on
  // those would make the linter something to disable rather than obey.
  //
  // Since ADR-0009 a bare reference means unambiguously "in this repo" — the other-repo
  // case has its own syntax — so the remaining obstacle to erroring here is boxel's
  // legacy volume alone, not the mechanism.
  proseRefs(docs, root, report) {
    const ids = new Set(docs.filter((d) => d.ok && d.data.id !== undefined).map((d) => normId(d.data.id)));
    const self = repoName(root);
    for (const d of docs) {
      if (!d.ok || !d.body) continue;
      const unresolved = new Set();
      for (const id of localCitations(d.body, self)) {
        if (!ids.has(id)) unresolved.add(id);
      }
      for (const ref of [...unresolved].sort()) {
        report('warn', d.path, `R9 prose references ADR ${ref}, which does not exist`);
      }
    }
  },
};

// --- runner -----------------------------------------------------------------

export function lint(root) {
  const docs = loadDocs(root);
  const findings = [];
  const report = (severity, path, message) => findings.push({ severity, path, message });
  for (const rule of Object.values(rules)) rule(docs, root, report);
  return { docs, findings };
}

function main(argv) {
  const quiet = argv.includes('--quiet');
  const roots = argv.filter((a) => !a.startsWith('--'));
  if (roots.length === 0) roots.push(process.cwd());

  let errors = 0;
  let warnings = 0;

  for (const root of roots) {
    const { docs, findings } = lint(root);
    const errs = findings.filter((f) => f.severity === 'error');
    const warns = findings.filter((f) => f.severity === 'warn');
    errors += errs.length;
    warnings += warns.length;

    if (!quiet || findings.length) {
      console.log(`\n${root} — ${docs.length} document(s)`);
    }
    for (const f of [...errs, ...warns]) {
      const tag = f.severity === 'error' ? 'ERROR' : ' WARN';
      // Corpus-level findings are reported against the root itself; basename would render
      // it as "adr: no adr/ directory here", which reads as a contradiction.
      const where = f.path === root ? root : basename(f.path);
      console.log(`  ${tag}  ${where}: ${f.message}`);
    }
    if (!findings.length && !quiet) console.log('  ok');
  }

  const summary = `\n${errors} error(s), ${warnings} warning(s)`;
  if (!quiet || errors || warnings) console.log(summary);
  return errors > 0 ? 1 : 0;
}

// realpath, not a string compare on argv[1]: invoked through a bin symlink the naive form
// silently does nothing, which is how `npx stele` shipped as a no-op (ADR-0015).
if (realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main(process.argv.slice(2)));
}
