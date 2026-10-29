export const meta = {
  name: 'dovastash-design-judge',
  description: 'Judge the Dova Stash Editor against the impeccable and unslop-ui design skills from screenshots, then fix what the judge finds, round after round',
  phases: [
    { title: 'Capture', model: 'opus', detail: 'build editor-release, take the fixed screenshot set' },
    { title: 'Judge', model: 'opus', detail: 'impeccable critique (Assessment A) + unslop checklist, scores and issues' },
    { title: 'Fix', model: 'opus', detail: 'the editor lane applies the issues, rebuilds, re-shoots' },
  ],
}

// docs/DEV-CYCLE.md, "Design judge". The judge reads the two vendored skills under .claude/skills and the editor's
// own design records (PRODUCT.md, DESIGN.md, docs/EDITOR-UX.md §3.5). It never runs impeccable's launcher, detector,
// live mode or hook (user rule: the editor is Dear ImGui, not a web page): the critique is Assessment A alone, from
// screenshots and code, and the run says so.

const MAX_ROUNDS = 20
const CALIBRATED_FROM = 13  // from this round only perceptible issues count, earlier fixes are not re-tuned (rounds 7-12 stalled at 8 on pixel measurements, some flipping a previous fix)
const EXHAUSTIVE_FROM = 7  // from this round the judge lists everything at once (rounds 1-6 drip-fed a few minors each and stalled at 8)
const TARGET = 10  // overall score (1-10) at which the loop stops

// Standing decisions (the owner's): the judge does not raise these again and the fixer does not do them.
const DECISIONS = [
  'The fill colours of the plan stay as they are (status hues): a gray ramp would vanish over the 3D textures.',
  'The rail keeps one-home mod groups (mod row + home row): the mod name is what authors scan for.',
  'Brand exceptions stand (EDITOR-UX.md §3.5): Cinzel/Jost tracked capitals for the wordmark, titles and section heads; 18 px icons; the emblem as the only ornament.',
  'The editor is Dear ImGui: no CSS, no web fonts, no browser focus rings; fixes are in the widget vocabulary of editor/src/ui/Widgets.h.',
]
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
  // States a person reaches by typing and clicking (2026-10-29: a rail search ending on a home's "All cells..." row
  // raised a Dear ImGui error the static shots above never reached, after a 10/10).
  ['search-home.png', "--search whitepeak --home 'Whitepeak Tower'"],
  ['search-mod.png', '--search aldamar'],
  ['search-none.png', '--search zzznomatch'],
  ['search-narrow.png', "--search whitepeak --home 'Whitepeak Tower' --window 1366x768 --text-size 150"],
  ['layers.png', '--home Aldamar --popover layers'],
  ['plan-search.png', "--search lakeview --home 'Lakeview Manor' --tab plan"],
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

You are the CAPTURE agent for round ${round} of the design judge. Build editor-release (report the result; stop with failures if it fails), then take these screenshots into the folder ${dir} (create it), one run each, with the common flags and a fresh DOVASTASH_EDITOR_STATE file name for every run (a new random name each time rather than deleting, which may be blocked, so the start panel shows no leftovers); only one editor instance at a time:
${SHOTS.map(([n, f]) => `- ${n}: ${f || '(no extra flags)'}`).join('\n')}
Record each run's exit code. Exit 5 means Dear ImGui reported an error (the red "Message from Dear ImGui" box a user sees): copy every "imgui error:" line from the run's editor.log (next to its state file) into that shot's note and into failures, and keep going with the other shots. Any other non-zero exit is a failure too. Return the absolute path of every file, whether it exists and is a non-trivial PNG, and its exit code in the note. Do not fix anything.`
}

function judgePrompt(round, capture, prev) {
  return `${COMMON}

You are the DESIGN JUDGE for round ${round}. Invoke the Skill tool with skill "impeccable" first so its instructions load, then read, in this order: .claude/skills/impeccable/reference/critique.md (the "Assessment A: Design Review" section, the Heuristics Scoring Guide and the Cognitive Load Assessment), .claude/skills/impeccable/reference/operate.md, .claude/skills/impeccable/reference/craft-floor.md, .claude/skills/unslop-ui/SKILL.md (its Quick-Scan matrix and the Complete Verification Checklist), then PRODUCT.md, DESIGN.md, docs/EDITOR-UX.md §3.5 and presskit/README.md "Style".
This is Assessment A alone, on a native desktop surface: there is no detector, no browser and no live mode here, so do not run them and do not call the run degraded; say "Assessment A only (Dear ImGui desktop app, no web detector)" in provenance.

Look at every screenshot (Read each PNG): ${JSON.stringify(capture.shots)}
${capture.failures.length ? `Dear ImGui errors in this round's runs (each is a MAJOR issue, the overall score is at most 5 while any remains, and the fix is in the code that drew that window): ${JSON.stringify(capture.failures)}` : 'No Dear ImGui errors in this round's runs.'}
The shots are still frames: a score covers only what they show. In whatA10Needs, name any state a person reaches that no shot covers (hover, a popover, an empty or overflowing list, a long name, a narrow window) that you could not judge.
Then read the code that draws what you question (editor/src/ui/*.cpp, Theme.cpp and Widgets.cpp for the tokens and widgets) so each issue names a file and line.
${prev ? `Last round's result: overall ${prev.overall}, issues ${JSON.stringify(prev.issues.map(i => i.title))}. Check whether each was really fixed before scoring again.` : ''}

Standing decisions, settled by the owner; do not raise them again and do not score against them:
${DECISIONS.map(d => '- ' + d).join('\n')}

Judge as a demanding design director in Operate mode (a tool; the brand's rules in EDITOR-UX.md §3.5 override the skills' defaults where they say so: tracked capitals for the wordmark, titles and section heads, 18 px icons). Score the ten Nielsen heuristics 0-4 with a sentence each, run the unslop checklist (pass/fail per item with a note), give designSpecificity (could another tool wear this skin unchanged?), 2-3 strengths, and an overall 1-10 where 10 means you looked hard and found nothing left. The score and the issues must agree: a 10 has no issues; a 9 has nits only; an 8 has minors; any major caps the score at 7. whatA10Needs is a numbered list, one line per gap, each a change the fixer can make this round (if nothing is left, say "Nothing: 10/10" and return no issues).

${round >= CALIBRATED_FROM ? `CALIBRATION (rounds 7-12 stalled at 8 because the judge measured pixels and re-tuned its own earlier fixes):
- Report only what a user notices at 100 % without a ruler or a zoomed crop: something misread, hard to find, inconsistent between two places they compare, clipped, illegible, or plainly misaligned (4 px or more against a visible neighbour edge). A 1-3 px offset, a gap of 8 against 12, a radius of 3 against 4, a 2 % shade difference are NOT issues, whatever a grid rule says.
- Severity is the user's: major = they pause, misread or err; minor = they notice it in normal use and it looks wrong; nit = visible when they look for it. Under the score rule, nits alone still allow a 9; a 10 needs nothing a user would notice.
- Do not reopen, re-tune or reverse an earlier round's fix (the titles are below) unless it is broken (clipped, wrong, unreadable); "the fix landed at 13 px, not 12" is not broken.
- Earlier rounds' issues, all fixed: ${JSON.stringify(history.flatMap(h => h.judge.issues.map(i => 'r' + h.round + ': ' + i.title)))}
` : ''}Every issue must be ACTIONABLE, which means all of: (1) location is a file:line in editor/src (open the file and quote the line) plus the screenshot and region where it shows; (2) evidence is what is on screen now, measured where it can be (px sizes, colours, counts, the words shown); (3) fix names the exact widget or token to change (Widgets.h / Theme.cpp names), the new values or words, and what the screenshot will show afterwards so the fixer can check it; (4) it is one change, not a theme ("tighten spacing" is not an issue; "ItemSpacing.y 4 to 8 between the Label field and the Ignored check, Roles.cpp:410, so the field groups read as 8-px pairs" is). Drop anything you cannot state that way. Do not raise an issue the standing decisions settle, one that needs the web, or one the brand rules in §3.5 permit. ${round >= EXHAUSTIVE_FROM ? 'Be EXHAUSTIVE: walk every screenshot region by region (title row, rail, toolbar, view, hint line, inspector, drawer, status row, popovers) and list every actionable issue you find now, all severities, in this one pass; do not hold any back for a later round. The loop stops only at 10, and each round you hold back costs a full rebuild.' : 'Prefer the fewer, sharper issues: if the editor is a 9, say so and list the nits.'} Prefer structural fixes a data grid or a desktop tool would have (filters in column headers, not chip rows; counts as text; state as dots) over decoration. Do not propose web-only things (CSS, fonts the app cannot load, browser focus rings). Return only the structured output.`
}

function fixPrompt(round, judge) {
  return `${COMMON}

You are the FIXER (editor lane) for round ${round} of the design judge. Scope: editor/**, docs/EDITOR.md, docs/EDITOR-UX.md (keep §3.5 and §2 "Built" paragraphs true to what you change; put lines for other docs in docLines). Use small exact edits; follow the widget vocabulary in editor/src/ui/Widgets.h and the conventions at the top of Widgets.cpp and Theme.cpp (tabs, C++23, comments in the file's voice); never add a web dependency. After the edits: build editor-release, fix warnings, then re-shoot the screenshots whose issues you fixed (same flags as the capture) and look at them to confirm.

Issues from the judge, most severe first:
${JSON.stringify(judge.issues, null, 1)}

Standing decisions (do not do these even if an issue asks):
${DECISIONS.map(d => '- ' + d).join('\n')}

${round >= EXHAUSTIVE_FROM ? 'Scope also covers user-facing text that src/core builds for the editor (for example the draft reasons in src/core/src/DraftSuggest.cpp): change only the wording or order, then build and run the core tests (cmake --build --preset core-tests; ctest --preset core-tests) and update any test that matches the old text. ' : ''}Fix every issue whose fix is sound and inside scope; skip one only when it contradicts EDITOR-UX.md §3.5, the brand, a standing decision or a user rule, and say why. The build must end with zero warnings; run ctest --test-dir build\editor -C Release. Look at every re-shot screenshot with the Read tool and confirm it shows what the issue's fix promised; fix what it does not. Return only the structured output.`
}

const history = []
let prev = null
for (let round = 1; round <= MAX_ROUNDS; round++) {
  const dir = `${REPO}\\build\\design-judge\\r${round}`
  log(`Round ${round}: capture`)
  const capture = await agent(capturePrompt(round, dir), { label: `capture r${round}`, model: 'opus', phase: 'Capture', schema: CAPTURE_SCHEMA })
  if (!capture) { log('capture agent died'); break }
  // An ImGui error is a blocker the judge must see and the fixer must fix; anything else that failed stops the run.
  const imguiFailures = capture.failures.filter(f => /imgui/i.test(f))
  if (capture.failures.length > imguiFailures.length) { log(`capture failed: ${capture.failures.join('; ')}`); break }

  log(`Round ${round}: judge`)
  const judge = await agent(judgePrompt(round, capture, prev), { label: `judge r${round}`, model: 'opus', phase: 'Judge', schema: JUDGE_SCHEMA })
  if (!judge) { log('judge agent died; stopping'); break }
  log(`Round ${round}: overall ${judge.overall}/10, ${judge.issues.length} issues; ${judge.designSpecificity}`)
  history.push({ round, capture, judge })
  prev = judge

  if (judge.overall >= TARGET || judge.issues.length === 0) { log(`Round ${round}: ${judge.overall}/10, target ${TARGET} reached. Done.`); break }

  const order = { major: 0, minor: 1, nit: 2 }
  const issues = [...judge.issues].sort((a, b) => order[a.severity] - order[b.severity])
  log(`Round ${round}: fixing ${issues.length} issues`)
  const fix = await agent(fixPrompt(round, { ...judge, issues }), { label: `fix r${round}`, model: 'opus', phase: 'Fix', schema: FIX_SCHEMA })
  history[history.length - 1].fix = fix
  if (fix) log(`Round ${round}: fixed ${fix.fixed.length}, skipped ${fix.skipped.length}; ${fix.buildResult}`)
}

return { rounds: history.length, final: history[history.length - 1] && history[history.length - 1].judge.overall, history }
