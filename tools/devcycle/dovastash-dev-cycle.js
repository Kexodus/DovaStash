export const meta = {
  name: 'dovastash-dev-cycle',
  description: 'Verify, grade (ease of use + functionality 1-10) and fix every aspect of Dova Stash, round after round, until every grader gives 10/10',
  phases: [
    { title: 'Verify', model: 'opus', detail: 'builds, unit tests, data checks, full in-game suite, editor screenshots' },
    { title: 'Grade', model: 'opus', detail: 'one grader per area, two scores 1-10, concrete issues' },
    { title: 'Fix', model: 'opus', detail: 'one fixer per lane (core+data, plugin, editor), then docs' },
  ],
}

const MAX_ROUNDS = 6
const REPO = 'E:\\AI Projects\\Dovastash'

const COMMON = `Repo: ${REPO}, branch editor-ux, working tree has a large uncommitted batch (today's hotkey menu, press-to-bind, templates, panel reorganisation and editor texture work). NEVER commit, stash, reset, checkout or revert anything; never run git fetch/pull or shallow commands. Project rules you must honour: never lose an item (when in doubt don't move it); in-game text terse (no prefixes, one short entry); the in-game UI borrows the player's own UI mods, never our own skin; every setting live without restart; a change updates every copy of it (DESIGN, TESTING, CHANGELOG, Nexus text in presskit/, examples, tests) in the same work; Menu Framework panel text follows the conventions at the top of src/plugin/UI/Panel/Strings.h. Builds: core '\`cmake --preset core-tests && cmake --build --preset core-tests && ctest --preset core-tests\`', plugin '\`cmake --preset plugin && cmake --build --preset plugin-release\`' (deploys the DLL and data to the MO2 mod folder itself), editor '\`cmake --preset editor && cmake --build --preset editor-release\`' (VCPKG_ROOT=E:\\vcpkg). In-game suite: tools/autotest/run.ps1 (see the ingame-test skill: invoke the Skill tool with skill "ingame-test" before running it); only ONE game may run at a time on this machine: before launching, check for SkyrimSE.exe / ModOrganizer.exe processes and wait, polling every 60 s up to 30 minutes. Do not rebuild the user guide PDF or retake its figures.`

const DECISIONS = `User decisions (2026-10-22), binding for grading and fixing:
1. Displays in templates are OPTIONAL. A template may list racks, mannequins, plaques and bookshelves, but no shipped template has to; players may enjoy choosing what to mount themselves. Do not count "shipped templates list no displays" as an issue; do count it if a template CANNOT express displays or the editor cannot add them.
2. No templates for the Lux-list mod homes for now. Do not count their absence as an issue.
3. Multi-cell homes MUST ship as one merged template per home using the multi-cell schema (DESIGN §6.7, "cells"): Hearthfire homes (hall, cellar, grounds), Tundra Homestead and its cellar, Myrwatch and its gallery, and any other shipped pair. Count it until done; the core lane does the merge, validates with the data checker, and the plugin lane's in-game plans 21-23 and 38 must pass on the merged files.`

const VERIFY_SCHEMA = {
  type: 'object',
  properties: {
    coreTests: { type: 'string' },
    pluginBuild: { type: 'string' },
    editorBuild: { type: 'string' },
    dataChecks: { type: 'string' },
    ingame: { type: 'string' },
    editorScreenshots: { type: 'array', items: { type: 'string' } },
    failures: { type: 'array', items: { type: 'string' } },
    allGreen: { type: 'boolean' },
  },
  required: ['coreTests', 'pluginBuild', 'editorBuild', 'dataChecks', 'ingame', 'editorScreenshots', 'failures', 'allGreen'],
}

const GRADE_SCHEMA = {
  type: 'object',
  properties: {
    area: { type: 'string' },
    easeOfUse: { type: 'integer', minimum: 1, maximum: 10 },
    functionality: { type: 'integer', minimum: 1, maximum: 10 },
    whatA10Needs: { type: 'string' },
    issues: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          title: { type: 'string' },
          severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
          lane: { type: 'string', enum: ['core', 'plugin', 'editor', 'docs'] },
          location: { type: 'string' },
          evidence: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['title', 'severity', 'lane', 'location', 'evidence', 'fix'],
      },
    },
  },
  required: ['area', 'easeOfUse', 'functionality', 'whatA10Needs', 'issues'],
}

const FIX_SCHEMA = {
  type: 'object',
  properties: {
    lane: { type: 'string' },
    fixed: { type: 'array', items: { type: 'string' } },
    skipped: { type: 'array', items: { type: 'string' } },
    filesChanged: { type: 'array', items: { type: 'string' } },
    buildResult: { type: 'string' },
    docLinesForDocsLane: { type: 'array', items: { type: 'string' } },
  },
  required: ['lane', 'fixed', 'skipped', 'filesChanged', 'buildResult', 'docLinesForDocsLane'],
}

const AREAS = [
  { key: 'core-engine', lanes: 'core', brief: 'Core sorting logic in src/core (category engine, keep rules with quotas and restock, sort planner, layout/template schema and resolution incl. hiddenTemplates, multi-cell homes) and its Catch2 tests in tests/. Functionality: correctness, edge cases, "never lose an item". Ease of use: how understandable the JSON schemas and diagnostics are for a template author (docs/DESIGN.md §6-7).' },
  { key: 'gameplay', lanes: 'plugin', brief: 'What a player experiences in game: Deposit, Sort all, Retrieve, Restock, sell chest, displays (racks, mannequins, plaques, bookshelves), craft from storage, learning with undo, HUD lines, auto setup on first visit, multi-cell homes. Use the in-game suite results and the plans under tests/ingame and src/plugin/AutoTest, the Controller and Sort code in src/plugin, and the research notes in docs/research/playtest-2026-10-14.md (its still-open potential bugs count against functionality until fixed).' },
  { key: 'hotkey-menus', lanes: 'plugin', brief: 'The hotkey menus: UIExtensions lists (src/plugin/UI/ListUI.cpp, UIExtensions.cpp) and the message-box fallback (MessageBoxUI.cpp): home menu, Advanced list, container menu, display menu, category pickers, template offers. Ease of use: entry count, wording, order, back paths, confirmations, discoverability of Advanced. Functionality: every entry does what it says, nothing dead, nothing blocking other menus.' },
  { key: 'panel', lanes: 'plugin', brief: 'The SKSE Menu Framework panel (src/plugin/UI/Panel/*, four pages: This home, Displays, Settings, About) against the conventions at the top of Strings.h and DESIGN §10. Ease of use: can a player find Sort, Retrieve, templates, keep rules, hotkey binding without reading docs; wording; tooltips; empty states; confirmations. Functionality: every control works and saves live, including press-to-bind hotkeys, templates Use/Re-apply/Remove/Add back, Only installed homes filter, the aimed container, the armor set picker on Displays. It is ImGui so you cannot screenshot it; grade from code and strings and say so.' },
  { key: 'editor', lanes: 'editor', brief: 'The Dova Stash Editor (editor/, spec docs/EDITOR.md and docs/EDITOR-UX.md): run it headless against the MO2 instance with --home and --screenshot (see EDITOR.md §9 and editor/src/Cli.cpp for the exact flags; run it outside MO2) over at least Breezehome, Wuth Rein and one Hearthfire home, and look at the screenshots. Ease of use: first-run flow, home list, draft review, template saving, checks, command palette, text size. Functionality: scan, drafts, 3D view textures and meshes, save, checks, CLI.' },
  { key: 'data-templates', lanes: 'core', brief: 'Shipped data in dist/Data/SKSE/Plugins/DovaStash (Categories/*.json, Templates/**.json) and dist/Optional/OCF: validate every file with the project checker (find the command in docs/TESTING.md or tools/esm_dump.py check), and judge coverage and quality of the templates for the homes the user prioritises (vanilla, Hearthfire, Creation Club, and the mod homes under docs/research and the Lux patch hub list). Functionality: files validate, categories complete, no dead form refs. Ease of use: readable templates, consistent naming.' },
  { key: 'docs-onboarding', lanes: 'docs', brief: 'README.md, CHANGELOG.md, docs/DESIGN.md, docs/TESTING.md, presskit/nexus-*.txt. Ease of use: can a new player install and use the mod from the Nexus description and README alone; are the pages, menus and settings described as they are now (four panel pages, Advanced list, press-to-bind, templates Remove/Add back); is the changelog in players words; any contradictions between docs and code. Functionality: accuracy against the code.' },
  { key: 'build-tooling', lanes: 'core', brief: 'Build and test tooling: CMake presets, the deploy step, tests/ingame harness and tools/autotest/run.ps1, tools/esm_dump.py, presskit/src/render.py, the ingame-test skill text under .claude/skills. Ease of use: one-command build/test for a contributor following docs/BUILDING.md; clear failures. Functionality: everything runs clean from a fresh shell; no stale paths; no flaky waits.' },
]

function verifyPrompt(round) {
  return `${COMMON}

You are the VERIFY agent for round ${round} of an automated dev cycle. Run, in this order, and report results exactly (verbatim failure output, trimmed to the failing lines):
1. Core tests: configure, build and ctest the core-tests preset.
2. Plugin: configure and build plugin-release (it deploys to MO2).
3. Editor: configure and build editor-release.
4. Data checks: validate every shipped template and category file with the project's checker (find the command in docs/TESTING.md or tools/esm_dump.py --help; run python with -I).
5. In-game: invoke the Skill tool with skill "ingame-test", then run the FULL unattended suite with the DLL just built (wait for no running game first). Report pass/fail/manual counts and every failure verbatim with its plan and test id.
6. Editor screenshots: run the editor headless (see docs/EDITOR.md §9 and editor/src/Cli.cpp) for Breezehome, Wuth Rein and Lakeview Manor with --screenshot into the scratchpad directory; return the absolute paths.
Do not fix anything. If a step cannot run, say why in failures. allGreen is true only when every step passed with zero failures.`
}

function gradePrompt(area, round, verify, prevScores) {
  return `${COMMON}

You are the GRADER for the area "${area.key}" in round ${round} of an automated dev cycle. Area brief: ${area.brief}
${round >= 2 ? DECISIONS + String.fromCharCode(10) : ''}
Latest verify results (this round): ${JSON.stringify(verify)}
${prevScores ? `Your area's previous scores: ${JSON.stringify(prevScores)}. Re-check whether last round's issues were really fixed before re-scoring.` : ''}

Grade two things, each 1-10, as a demanding reviewer would: easeOfUse (can a player or author do the job without reading docs, with terse clear text and sensible defaults) and functionality (does everything in this area work, with evidence). A 10 means you looked hard and found nothing left to improve; do not give 10 out of politeness, and do not withhold it when the area truly is done. Read the actual code, tests, data and docs; run read-only commands as needed (tests you run must not collide with other agents: do NOT run cmake builds or the game; the verify step already did). For every point below 10 list a concrete issue with its location (file:line or data file or doc section), the evidence, and the exact fix, tagged with the lane that owns the files: core (src/core, tests/, dist/, tools/), plugin (src/plugin, tests/ingame), editor (editor/, docs/EDITOR*.md), docs (docs/DESIGN.md, docs/TESTING.md, CHANGELOG.md, README.md, presskit/). Severity: blocker (breaks the mod or loses items), major (a player would hit it), minor (polish). whatA10Needs: one paragraph. Return only the structured output.`
}

function fixPrompt(lane, issues, round, extraLines) {
  const scope = {
    core: 'src/core/**, tests/** (Catch2), dist/**, tools/** — build and run the core-tests preset after your changes; also run the data checker on dist/ if you touched data. Do NOT touch src/plugin, editor/, docs/ or CHANGELOG.',
    plugin: 'src/plugin/**, tests/ingame/**, tools/autotest/** — build plugin-release (deploys to MO2) and run the in-game plans that cover what you changed (ingame-test skill; one game at a time, wait for none running). Do NOT touch src/core, editor/, docs/ or CHANGELOG; put any doc lines in docLinesForDocsLane.',
    editor: 'editor/**, docs/EDITOR.md, docs/EDITOR-UX.md — build editor-release and verify with headless --screenshot runs. Do NOT touch src/, dist/, other docs or CHANGELOG; put changelog lines in docLinesForDocsLane.',
    docs: 'docs/DESIGN.md, docs/TESTING.md, CHANGELOG.md, README.md, presskit/*.txt and presskit/src (re-render images with python -I presskit/src/render.py --check-gray only if you changed a source) — bring every description in line with the code as it is NOW (read the code; other lanes changed it this round). Apply the lines the other lanes reported. Do NOT touch src/, editor/, dist/, tests/.',
  }[lane]
  return `${COMMON}

You are the FIXER for lane "${lane}" in round ${round}. Your scope and verification duty: ${scope}
Other fixers are editing other lanes' files right now in the same working tree; stay inside your scope, use small exact edits, never rewrite whole files, and if a build fails on code outside your scope wait 60 s and retry before investigating.

${round >= 2 ? DECISIONS + String.fromCharCode(10) : ''}Issues to fix (from the graders), most severe first:
${JSON.stringify(issues, null, 1)}
${extraLines && extraLines.length ? `\nLines other lanes asked the docs lane to apply:\n${extraLines.join('\n')}` : ''}

Fix every issue whose fix is sound; if one is wrong or would violate a project rule, skip it and say why in skipped. Keep text terse and consistent with the conventions. Leave the build green. Return only the structured output.`
}

const history = []
let prev = null
for (let round = 1; round <= MAX_ROUNDS; round++) {
  log(`Round ${round}: verify`)
  const verify = await agent(verifyPrompt(round), { label: `verify r${round}`, model: 'opus', phase: 'Verify', schema: VERIFY_SCHEMA })
  if (!verify) { log('verify agent died; stopping'); break }

  log(`Round ${round}: grading ${AREAS.length} areas`)
  const grades = (await parallel(AREAS.map(a => () =>
    agent(gradePrompt(a, round, verify, prev && prev[a.key]), { label: `grade:${a.key} r${round}`, model: 'opus', phase: 'Grade', schema: GRADE_SCHEMA })
  ))).filter(Boolean)

  const scores = {}
  for (const g of grades) scores[g.area] = { easeOfUse: g.easeOfUse, functionality: g.functionality, whatA10Needs: g.whatA10Needs }
  const table = grades.map(g => `${g.area}: ease ${g.easeOfUse}/10, function ${g.functionality}/10, ${g.issues.length} issues`).join(' | ')
  log(`Round ${round} scores: ${table}`)
  history.push({ round, verify, scores, issues: grades.flatMap(g => g.issues) })
  prev = scores

  const allTen = grades.length === AREAS.length && grades.every(g => g.easeOfUse === 10 && g.functionality === 10) && verify.allGreen
  if (allTen) { log(`Round ${round}: every area is 10/10 and verify is green. Done.`); break }

  const order = { blocker: 0, major: 1, minor: 2 }
  const byLane = { core: [], plugin: [], editor: [], docs: [] }
  for (const g of grades) for (const i of g.issues) byLane[i.lane].push({ area: g.area, ...i })
  for (const k of Object.keys(byLane)) byLane[k].sort((a, b) => order[a.severity] - order[b.severity])

  log(`Round ${round}: fixing core ${byLane.core.length}, plugin ${byLane.plugin.length}, editor ${byLane.editor.length}; docs ${byLane.docs.length} after`)
  const codeLanes = ['core', 'plugin', 'editor'].filter(l => byLane[l].length)
  const fixes = (await parallel(codeLanes.map(l => () =>
    agent(fixPrompt(l, byLane[l], round, []), { label: `fix:${l} r${round}`, model: 'opus', phase: 'Fix', schema: FIX_SCHEMA })
  ))).filter(Boolean)
  const docLines = fixes.flatMap(f => f.docLinesForDocsLane)
  const docsIssues = byLane.docs
  if (docsIssues.length || docLines.length) {
    const d = await agent(fixPrompt('docs', docsIssues, round, docLines), { label: `fix:docs r${round}`, model: 'opus', phase: 'Fix', schema: FIX_SCHEMA })
    if (d) fixes.push(d)
  }
  history[history.length - 1].fixes = fixes
  log(`Round ${round}: fixed ${fixes.reduce((n, f) => n + f.fixed.length, 0)}, skipped ${fixes.reduce((n, f) => n + f.skipped.length, 0)}`)
}

return { rounds: history.length, final: history[history.length - 1] && history[history.length - 1].scores, history }