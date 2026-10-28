export const meta = {
  name: 'dovastash-design-judge',
  description: 'Judge the Dova Stash Editor against the impeccable and unslop-ui design skills from screenshots, then fix what the judge finds, round after round',
  phases: [
    { title: 'Capture', detail: 'build editor-release, take the fixed screenshot set' },
    { title: 'Judge', detail: 'impeccable critique (Assessment A) + unslop checklist, scores and issues' },
    { title: 'Fix', detail: 'the editor lane applies the issues, rebuilds, re-shoots' },
  ],
}

// docs/DEV-CYCLE.md, "Design judge". The judge reads the two vendored skills under .claude/skills and the editor's
// own design records (PRODUCT.md, DESIGN.md, docs/EDITOR-UX.md §3.5). It never runs impeccable's launcher, detector,
// live mode or hook (user rule: the editor is Dear ImGui, not a web page): the critique is Assessment A alone, from
// screenshots and code, and the run says so.

const MAX_ROUNDS = 3
const TARGET = 9  // overall score (1-10) at which the loop stops
const REPO = 'E:\\AI Projects\\Dovastash'

const COMMON = `Repo: ${REPO}. NEVER commit, stash, reset, checkout or revert anything; never run git fetch/pull. Rules: the editor's look is specified in docs/EDITOR-UX.md (§3 visual language, §3.5 the design protocol settled with the unslop-ui and impeccable skills, both vendored under .claude/skills); PRODUCT.md and DESIGN.md at the repo root are impeccable's context files; the brand is presskit/README.md "Style" (black, white and neutral grays, Cinzel tracked capitals for the wordmark and titles, Jost for text, the emblem as the only ornament). The editor is a Win32 + Dear ImGui app: there is no CSS, DOM or browser, so do NOT run impeccable's launcher (scripts/impeccable, impeccable.cmd), its detector, live mode or hook, and do not try to open anything in a browser. Build: '\`cmake --build --preset editor-release\`' (VCPKG_ROOT=E:\\vcpkg; configure first with '\`cmake --preset editor\`' if build/editor is missing). Screenshots: run build\\editor\\editor\\Release\\DovaStashEditor.exe with --mo2 'E:\\modding\\Kexodus Skyrim' --profile 'Dova Stash Testing' --gui --window 1600x900 and --screenshot <file>, with DOVASTASH_EDITOR_STATE set to a fresh temp file so the user's state.json stays untouched (the recipe and flags: docs/design-pass/README.md, docs/EDITOR.md §9, editor/src/Cli.cpp). Do not rebuild the user guide PDF or retake presskit/guide figures.`

// The fixed set every round shoots (file name: flags after the common ones).
const SHOTS = [
  ['start.png', ''],
  ['selection.png', "--home Aldamar --tab 3d --select 'Eli_Druid Home.esp|0x00000B1B'"],
  ['containers.png', "--home Aldamar --tab containers --select 'Eli_Druid Home.esp|0x00000B1B'"],
  ['containers-filter.png', "--home Aldamar --tab containers --popover kind"],
  ['displays.png', "--home Aldamar --tab displays"],
  ['preview.png', "--home Aldamar --tab preview"],
  ['plan.png', "--home 'Whitepeak Tower' --tab plan --colours fill --popover colours"],
  ['drafts.png', '--drafts --home Aldamar --inspector draft'],
  ['template.png', "--home 'Lakeview Manor' --inspector template"],
  ['checks.png', "--home 'Lakeview Manor' --inspector checks"],
  ['setup.png', '--popover setup'],
  ['narrow-150.png', "--home Aldamar --tab containers --window 1366x768 --text-size 150"],
]

const CAPTURE_SCHEMA = {
  type: 'object',
  properties: {
    buildResult: { type: 'string' },
    shots: { type: 'array', items: { type: 'object', properties: { name: { type: 'string' }, path: { type: 'string' }, ok: { type: 'boolean' }, note: { type: 'string' } }, required: ['name', 'path', 'ok'] } },
    failures: { type: 'array', items: { type: 'string' } },
  },
  required: ['buildResult', 'shots', 'failures'],
}

const JUDGE_SCHEMA = {
  type: 'object',
  properties: {
    provenance: { type: 'string' },
    designSpecificity: { type: 'string' },
    heuristics: {
      type: 'array',
      items: { type: 'object', properties: { name: { type: 'string' }, score: { type: 'integer', minimum: 0, maximum: 4 }, why: { type: 'string' } }, required: ['name', 'score', 'why'] },
    },
    unslopChecklist: {
      type: 'array',
      items: { type: 'object', properties: { check: { type: 'string' }, pass: { type: 'boolean' }, note: { type: 'string' } }, required: ['check', 'pass'] },
    },
    overall: { type: 'integer', minimum: 1, maximum: 10 },
    whatA10Needs: { type: 'string' },
    strengths: { type: 'array', items: { type: 'string' } },
    issues: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          title: { type: 'string' },
          severity: { type: 'string', enum: ['major', 'minor', 'nit'] },
          rule: { type: 'string' },
          lane: { type: 'string', enum: ['editor', 'docs'] },
          location: { type: 'string' },
          evidence: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['title', 'severity', 'rule', 'lane', 'location', 'evidence', 'fix'],
      },
    },
  },
  required: ['provenance', 'designSpecificity', 'heuristics', 'unslopChecklist', 'overall', 'whatA10Needs', 'strengths', 'issues'],
}

const FIX_SCHEMA = {
  type: 'object',
  properties: {
    fixed: { type: 'array', items: { type: 'string' } },
    skipped: { type: 'array', items: { type: 'string' } },
    filesChanged: { type: 'array', items: { type: 'string' } },
    buildResult: { type: 'string' },
    docLines: { type: 'array', items: { type: 'string' } },
  },
  required: ['fixed', 'skipped', 'filesChanged', 'buildResult', 'docLines'],
}

function capturePrompt(round, dir) {
  return `${COMMON}

You are the CAPTURE agent for round ${round} of the design judge. Build editor-release (report the result; stop with failures if it fails), then take these screenshots into the folder ${dir} (create it), one run each, with the common flags and a fresh DOVASTASH_EDITOR_STATE file for every run (delete it between runs so the start panel shows no leftovers):
${SHOTS.map(([n, f]) => `- ${n}: ${f || '(no extra flags)'}`).join('\n')}
Return the absolute path of every file and whether it exists and is a non-trivial PNG. Do not fix anything.`
}

function judgePrompt(round, capture, prev) {
  return `${COMMON}

You are the DESIGN JUDGE for round ${round}. Invoke the Skill tool with skill "impeccable" first so its instructions load, then read, in this order: .claude/skills/impeccable/reference/critique.md (the "Assessment A: Design Review" section, the Heuristics Scoring Guide and the Cognitive Load Assessment), .claude/skills/impeccable/reference/operate.md, .claude/skills/impeccable/reference/craft-floor.md, .claude/skills/unslop-ui/SKILL.md (its Quick-Scan matrix and the Complete Verification Checklist), then PRODUCT.md, DESIGN.md, docs/EDITOR-UX.md §3.5 and presskit/README.md "Style".
This is Assessment A alone, on a native desktop surface: there is no detector, no browser and no live mode here, so do not run them and do not call the run degraded; say "Assessment A only (Dear ImGui desktop app, no web detector)" in provenance.

Look at every screenshot (Read each PNG): ${JSON.stringify(capture.shots)}
Then read the code that draws what you question (editor/src/ui/*.cpp, Theme.cpp and Widgets.cpp for the tokens and widgets) so each issue names a file and line.
${prev ? `Last round's result: overall ${prev.overall}, issues ${JSON.stringify(prev.issues.map(i => i.title))}. Check whether each was really fixed before scoring again.` : ''}

Judge as a demanding design director in Operate mode (a tool; the brand's rules in EDITOR-UX.md §3.5 override the skills' defaults where they say so: tracked capitals for the wordmark, titles and section heads, 18 px icons). Score the ten Nielsen heuristics 0-4 with a sentence each, run the unslop checklist (pass/fail per item with a note), give designSpecificity (could another tool wear this skin unchanged?), 2-3 strengths, and an overall 1-10 where 10 means you looked hard and found nothing left. For every issue: title, severity (major: a user pauses or errs; minor: polish; nit: taste), the rule it breaks (the skill or §3.5 line), lane (editor: editor/**, docs/EDITOR*.md; docs: other docs), location (file:line, or the screenshot and region), evidence, and the exact fix in the editor's own widget vocabulary (Widgets.h). Prefer structural fixes a data grid or a desktop tool would have (filters in column headers, not chip rows; counts as text; state as dots) over decoration. Do not propose web-only things (CSS, fonts the app cannot load, browser focus rings). Return only the structured output.`
}

function fixPrompt(round, judge) {
  return `${COMMON}

You are the FIXER (editor lane) for round ${round} of the design judge. Scope: editor/**, docs/EDITOR.md, docs/EDITOR-UX.md (keep §3.5 and §2 "Built" paragraphs true to what you change; put lines for other docs in docLines). Use small exact edits; follow the widget vocabulary in editor/src/ui/Widgets.h and the conventions at the top of Widgets.cpp and Theme.cpp (tabs, C++23, comments in the file's voice); never add a web dependency. After the edits: build editor-release, fix warnings, then re-shoot the screenshots whose issues you fixed (same flags as the capture) and look at them to confirm.

Issues from the judge, most severe first:
${JSON.stringify(judge.issues, null, 1)}

Fix every issue whose fix is sound and inside scope; skip one only when it contradicts EDITOR-UX.md §3.5, the brand, or a user rule, and say why. Return only the structured output.`
}

const history = []
let prev = null
for (let round = 1; round <= MAX_ROUNDS; round++) {
  const dir = `${REPO}\\build\\design-judge\\r${round}`
  log(`Round ${round}: capture`)
  const capture = await agent(capturePrompt(round, dir), { label: `capture r${round}`, phase: 'Capture', schema: CAPTURE_SCHEMA })
  if (!capture || capture.failures.length) { log(`capture failed: ${capture ? capture.failures.join('; ') : 'agent died'}`); break }

  log(`Round ${round}: judge`)
  const judge = await agent(judgePrompt(round, capture, prev), { label: `judge r${round}`, phase: 'Judge', schema: JUDGE_SCHEMA })
  if (!judge) { log('judge agent died; stopping'); break }
  log(`Round ${round}: overall ${judge.overall}/10, ${judge.issues.length} issues; ${judge.designSpecificity}`)
  history.push({ round, capture, judge })
  prev = judge

  if (judge.overall >= TARGET || judge.issues.length === 0) { log(`Round ${round}: ${judge.overall}/10, target ${TARGET} reached. Done.`); break }

  const order = { major: 0, minor: 1, nit: 2 }
  const issues = [...judge.issues].sort((a, b) => order[a.severity] - order[b.severity])
  log(`Round ${round}: fixing ${issues.length} issues`)
  const fix = await agent(fixPrompt(round, { ...judge, issues }), { label: `fix r${round}`, phase: 'Fix', schema: FIX_SCHEMA })
  history[history.length - 1].fix = fix
  if (fix) log(`Round ${round}: fixed ${fix.fixed.length}, skipped ${fix.skipped.length}; ${fix.buildResult}`)
}

return { rounds: history.length, final: history[history.length - 1] && history[history.length - 1].judge.overall, history }
