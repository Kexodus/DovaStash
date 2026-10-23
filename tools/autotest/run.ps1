<#
.SYNOPSIS
  Runs the automated in-game tests unattended (docs/TESTING.md, "Automated in-game tests").

.DESCRIPTION
  One run at a time on the machine; each step in full in docs/TESTING.md section 9.
  0. Takes the runner lock: busy (exit 2, or -WaitMinutes) while another run, a Skyrim or an AutoTest MO2 is up. Finishes a
     run stopped before its cleanup; moves what earlier runs left in MO2's overwrite folder into their run folders.
  1. -Build: builds the plugin (plugin-release preset).
  2. Installs the plans into the MO2 mod "Dova Stash AutoTest" (tests/ingame/install.ps1).
  3. Starts Skyrim through MO2 with the AutoTest profile and SKSE.
  4. Waits for results.json to say finished. Ends the game it started on the timeout, on a dialog nobody can answer, or
     when the game hangs (no write to results.json or DovaStash.log for -StallMinutes, or not responding for 2 min).
  5. Copies the logs and results into build\autotest\<yyyyMMdd-HHmmss>, prints the summary and writes it to summary.txt
     there (first line: which plans). Exits 1 when a test failed, a plan was blocked or the run did not finish.
  6. A watchdog (-Finish, a hidden process) does step 5 when the runner is killed, and ends a hung game.

.PARAMETER Mo2
  The MO2 instance folder (default: $env:DOVASTASH_MO2, else the developer's).
.PARAMETER ProfileName
  The MO2 profile the game runs in (default "Dova Stash AutoTest").
.PARAMETER Executable
  The MO2 executable to start (default "Launch Game").
.PARAMETER Build
  Builds the plugin first (plugin-release preset).
.PARAMETER Dll
  A DovaStash.dll of your own, put into the AutoTest mod for this profile only.
.PARAMETER TimeoutMinutes
  How long the game may take (default: _run.json timeoutMinutes plus 10).
.PARAMETER StallMinutes
  The game counts as hung after this long without a write to results.json or DovaStash.log (default 10).
.PARAMETER Focus
  Brings the game window to the front once (some menus open only with the focus).
.PARAMETER OutDir
  Where the run folders go (default build\autotest).
.PARAMETER Only
  A regex on plan file names: only those plans run ('^(21|22)_').
.PARAMETER Quarantine
  The default; still accepted.
.PARAMETER NoQuarantine
  Refuses to start while overwrite holds the player's setups, instead of setting them aside for the run.
.PARAMETER WaitMinutes
  Waits up to this long for the machine to be free (checks every 60 s); with -Finish, for the stopped run.
.PARAMETER PlayerProfile
  The profile to put back when ModOrganizer.ini already names the AutoTest one.
.PARAMETER AllowNotRun
  Plans left out for a plugin the profile lacks ("requires") do not fail the run.
.PARAMETER ListAll
  The summary prints every test, passed ones too (summary.txt always lists them).
.PARAMETER Finish
  Only cleans up after a run whose runner is gone (running.json still there), and starts no run: exit 0 (done) or 2.

.EXAMPLE
  pwsh -File tools\autotest\run.ps1
  pwsh -File tools\autotest\run.ps1 -Build
  pwsh -File tools\autotest\run.ps1 -Dll build\plugin\src\plugin\Release\DovaStash.dll
  pwsh -File tools\autotest\run.ps1 -Only '^(21|22)_' -WaitMinutes 30
  pwsh -File tools\autotest\run.ps1 -Finish -WaitMinutes 30
#>
# After the help: Get-Help ignores a help block that does not start the file.
#Requires -Version 7
[CmdletBinding()]
param(
	[string]$Mo2 = $(if ($env:DOVASTASH_MO2) { $env:DOVASTASH_MO2 } else { 'E:\modding\Kexodus Skyrim' }),
	[string]$ProfileName = 'Dova Stash AutoTest',
	[string]$Executable = 'Launch Game',
	[switch]$Build,
	[string]$Dll = '',
	[int]$TimeoutMinutes = 0,
	[int]$StallMinutes = 10,  # no write to results.json or DovaStash.log for this long: the game is hung and ended
	[switch]$Focus,
	[string]$OutDir = '',
	[string]$Only = '',
	[switch]$Quarantine,  # the default now; still accepted
	[switch]$NoQuarantine,
	[int]$WaitMinutes = 0,
	[string]$PlayerProfile = '',  # the profile to put back when ModOrganizer.ini already names the AutoTest one
	[switch]$AllowNotRun,  # plans left out for a missing plugin ("requires") do not fail the run
	[switch]$ListAll,  # the summary prints every test, passed ones too (summary.txt always lists them)
	[switch]$Finish  # only clean up after a run whose runner was killed (step 6), then exit
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..')
if (-not $OutDir) { $OutDir = Join-Path $repo 'build\autotest' }
$documents = [Environment]::GetFolderPath('MyDocuments')
$myGames = Join-Path $documents 'My Games\Skyrim Special Edition'
$skseDir = Join-Path $myGames 'SKSE'
$autoDir = Join-Path $skseDir 'DovaStash\AutoTest'
$results = Join-Path $autoDir 'results.json'
$mo2Exe = Join-Path $Mo2 'ModOrganizer.exe'
$mo2Ini = Join-Path $Mo2 'ModOrganizer.ini'
$overwrite = Join-Path $Mo2 'overwrite'
$dovaOverwrite = Join-Path $overwrite 'SKSE\Plugins\DovaStash'
# Machine-wide, not per checkout: every worktree's runner shares MO2's overwrite folder and the one game.
$stateDir = Join-Path ([IO.Path]::GetTempPath()) 'DovaStashAutoTest'
$lockPath = Join-Path $stateDir '.lock'
$statePath = Join-Path $stateDir 'running.json'
# When each run had the game up (UTC ticks), for the files a run could not move out of overwrite.
$historyPath = Join-Path $stateDir 'runs.json'
$ownProfileLine = "selected_profile=@ByteArray($ProfileName)"
# The player's own profile line, kept across runs: put back when ModOrganizer.ini already names the AutoTest one.
$playerProfilePath = Join-Path $stateDir 'player-profile.txt'
New-Item -ItemType Directory -Force $stateDir | Out-Null

# --- helpers ---------------------------------------------------------------------------------------------------
# Write-Error would end the script with exit code 1 (ErrorActionPreference Stop): the message, then the code.
function Stop-Run([string]$message, [int]$code) {
	$host.UI.WriteErrorLine($message)
	exit $code
}

if (-not (Test-Path (Join-Path $Mo2 'ModOrganizer.ini'))) { Stop-Run "No MO2 instance at ${Mo2}: pass -Mo2 <instance folder> or set DOVASTASH_MO2" 2 }

function Get-Busy {
	if (Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {
		return 'a Skyrim (SkyrimSE.exe) is running (the test run does not touch a game it did not start)'
	}
	$pattern = '-p\s+"?' + [regex]::Escape($ProfileName)
	$mo2Test = Get-CimInstance Win32_Process -Filter "Name = 'ModOrganizer.exe'" -ErrorAction SilentlyContinue |
		Where-Object { $_.CommandLine -match $pattern } | Select-Object -First 1
	if ($mo2Test) { return "an MO2 with the profile '$ProfileName' is running (pid $($mo2Test.ProcessId))" }
	return $null
}

function Open-Lock {
	try { return [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') } catch { return $null }
}

# Path -> last write (ticks) of every file in MO2's overwrite folder.
function Get-OverwriteSnapshot {
	$snap = @{}
	Get-ChildItem $overwrite -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $snap[$_.FullName] = $_.LastWriteTimeUtc.Ticks }
	return $snap
}

# The files that change what a run sees: layouts (a plan whose home has one is refused), home backups, and the
# AutoTest exports in Templates\User (they outrank the shipped templates).
function Get-Leftovers {
	$found = @()
	foreach ($sub in 'Layouts', 'Backups') {
		$dir = Join-Path $dovaOverwrite $sub
		if (Test-Path $dir) { $found += Get-ChildItem $dir -Recurse -File -Force }
	}
	$user = Join-Path $dovaOverwrite 'Templates\User'
	if (Test-Path $user) { $found += Get-ChildItem $user -File -Force -Filter 'AutoTest*' }
	return $found
}

# When earlier runs had the game up: the run folders (<OutDir>\yyyyMMdd-HHmmss until their results.json, which only
# the AutoTest game writes) and runs.json (runs from other checkouts). Two minutes of slack at the end.
function Get-RunWindows {
	$windows = @()
	$slack = [TimeSpan]::FromMinutes(2).Ticks
	if (Test-Path $historyPath) {
		try {
			foreach ($r in @(Get-Content $historyPath -Raw | ConvertFrom-Json)) {
				if ($r.start -and $r.end) { $windows += [pscustomobject]@{ start = [long]$r.start; end = [long]$r.end + $slack; runDir = "$($r.runDir)" } }
			}
		} catch { Write-Warning "$historyPath could not be read (ignored): $_" }
	}
	foreach ($d in Get-ChildItem $OutDir -Directory -ErrorAction SilentlyContinue) {
		$local = [datetime]::MinValue
		if (-not [datetime]::TryParseExact($d.Name, 'yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture, 'AssumeLocal', [ref]$local)) { continue }
		$res = Join-Path $d.FullName 'results.json'
		if (-not (Test-Path $res)) { continue }
		$windows += [pscustomobject]@{ start = $local.ToUniversalTime().Ticks; end = (Get-Item $res).LastWriteTimeUtc.Ticks + $slack; runDir = $d.FullName }
	}
	return $windows
}

# $null: not a test file. Else the folder of the earlier run that wrote it, or '' (an AutoTest file, run unknown).
function Get-EarlierRun([IO.FileInfo]$file, [object[]]$windows) {
	$t = $file.LastWriteTimeUtc.Ticks
	foreach ($w in $windows) { if ($t -ge $w.start -and $t -le $w.end -and $w.runDir -and (Test-Path $w.runDir)) { return $w.runDir } }
	if ($file.DirectoryName -eq (Join-Path $dovaOverwrite 'Templates\User') -and $file.Name -like 'AutoTest*') { return '' }
	try { $doc = Get-Content -Raw $file.FullName | ConvertFrom-Json } catch { return $null }
	if ("$($doc.name)" -match '\(AutoTest\)$|^AutoTest ' -or "$($doc.note)" -like 'AutoTest *') { return '' }
	return $null
}

# Adds this run's window to runs.json (the last 50 runs).
function Add-RunHistory([string]$runDir, [long]$startTicks) {
	$runs = @()
	if (Test-Path $historyPath) { try { $runs = @(Get-Content $historyPath -Raw | ConvertFrom-Json) } catch { $runs = @() } }
	$runs += [pscustomobject]@{ start = $startTicks; end = (Get-Date).ToUniversalTime().Ticks; runDir = $runDir }
	ConvertTo-Json -InputObject @($runs | Select-Object -Last 50) -Compress | Set-Content $historyPath -Encoding utf8NoBOM
}

function Restore-Profile([string]$line) {
	if ((-not $line -or $line -eq $ownProfileLine) -and (Test-Path $playerProfilePath)) {
		# An earlier run left ModOrganizer.ini on the AutoTest profile: the last player profile a run saw (a second line
		# "guessed": Find-PlayerProfile's guess, said again at every restore).
		$saved = @(Get-Content $playerProfilePath)
		$line = "$($saved[0])".Trim()
		if ($saved.Count -gt 1 -and "$($saved[1])".Trim() -eq 'guessed') { $script:profileGuessNote = ' (guessed; -PlayerProfile sets it)' }
	}
	if (-not $line) { return }
	if ($line -eq $ownProfileLine) {
		Write-Warning "ModOrganizer.ini: the profile saved to put back is '$ProfileName' itself (left as it is: pick your profile in MO2)"
		return
	}
	$ini = Get-Content $mo2Ini
	$now = $ini | Where-Object { $_ -like 'selected_profile=*' } | Select-Object -First 1
	if ($now -ne $line) {
		($ini | ForEach-Object { if ($_ -like 'selected_profile=*') { $line } else { $_ } }) | Set-Content $mo2Ini -Encoding utf8NoBOM
		Write-Host "ModOrganizer.ini: $line restored$script:profileGuessNote"
	}
}

# The player's profile when ModOrganizer.ini names the AutoTest one and no run saw another: -PlayerProfile, else
# install.ps1's -SourceProfile default when it exists, else the profile whose modlist.txt was written last.
$script:profileGuessNote = ''
function Find-PlayerProfile {
	if ($PlayerProfile) { return $PlayerProfile }
	$script:profileGuessNote = ' (guessed; -PlayerProfile sets it)'
	$sourceDefault = 'Dova Stash Testing'
	if (Test-Path (Join-Path $Mo2 "profiles\$sourceDefault\modlist.txt")) { return $sourceDefault }
	$newest = Get-ChildItem (Join-Path $Mo2 'profiles') -Directory -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -ne $ProfileName -and (Test-Path (Join-Path $_.FullName 'modlist.txt')) } |
		Sort-Object { (Get-Item (Join-Path $_.FullName 'modlist.txt')).LastWriteTimeUtc } -Descending |
		Select-Object -First 1
	if ($newest) { return $newest.Name }
	$script:profileGuessNote = ''
	return ''
}

# Moves the files that are new since the snapshot from MO2's overwrite folder into <run>\written (files that were
# there before are never touched), then puts the quarantined files back (never over a file: then they stay).
function Complete-Overwrite([hashtable]$before, [string]$runDir, [string[]]$quarantined) {
	$written = Join-Path $runDir 'written'
	foreach ($file in Get-ChildItem $overwrite -Recurse -File -Force -ErrorAction SilentlyContinue) {
		$relative = $file.FullName.Substring($overwrite.Length + 1)
		if (-not $before.ContainsKey($file.FullName)) {
			$dest = Join-Path $written $relative
			New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
			Move-Item -Force $file.FullName $dest
		} elseif ($before[$file.FullName] -ne $file.LastWriteTimeUtc.Ticks) {
			Write-Warning "overwrite\$relative was there before and changed during the run (left as it is)"
		}
	}
	$back = 0
	foreach ($relative in @($quarantined)) {
		if (-not $relative) { continue }
		$from = Join-Path $runDir "preexisting\$relative"
		$to = Join-Path $overwrite $relative
		if (-not (Test-Path $from)) { continue }
		if (Test-Path $to) { Write-Warning "overwrite\$relative exists again; the earlier copy stays in $from"; continue }
		New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
		Move-Item $from $to
		$back++
	}
	if ($back) { Write-Host "Put back $back file(s) set aside in $runDir\preexisting" }
}

function Save-RunLogs([string]$runDir, [datetime]$since) {
	New-Item -ItemType Directory -Force $runDir | Out-Null
	foreach ($f in (Join-Path $skseDir 'DovaStash.log'), $results, (Join-Path $myGames 'Logs\Script\Papyrus.0.log')) {
		if (Test-Path $f) { Copy-Item -Force $f $runDir }
	}
	if (Test-Path (Join-Path $autoDir 'Backups')) { Copy-Item -Recurse -Force (Join-Path $autoDir 'Backups') (Join-Path $runDir 'Backups') }
	$crashes = @(Get-ChildItem $skseDir -Filter 'crash-*.log' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $since })
	foreach ($c in $crashes) { Copy-Item -Force $c.FullName $runDir }
	return $crashes
}

function Read-Results {
	if (-not (Test-Path $results)) { return $null }
	try {
		# Read without locking: the game renames its new results over this file.
		$stream = [IO.File]::Open($results, 'Open', 'Read', 'ReadWrite, Delete')
		try { $text = (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
		return $text | ConvertFrom-Json
	} catch { return $null }
}

# A process that is still the one recorded (same id and start time: ids are reused).
function Test-Alive($procId, $startTicks) {
	return [bool]($procId -and (Get-Process -Id $procId -ErrorAction SilentlyContinue | Where-Object { $_.StartTime.ToUniversalTime().Ticks -eq $startTicks }))
}

# Adds keys to running.json (the game and MO2 this run started, for -Finish).
function Update-State([hashtable]$add) {
	try {
		$state = Get-Content $statePath -Raw | ConvertFrom-Json -AsHashtable
		foreach ($k in $add.Keys) { $state[$k] = $add[$k] }
		$state | ConvertTo-Json -Depth 3 -Compress | Set-Content $statePath -Encoding utf8NoBOM
	} catch { Write-Warning "$statePath could not be updated: $_" }
}

# The newest write (UTC) to results.json or DovaStash.log, not earlier than $since: a running plan writes both every
# few seconds, a plan takes about a minute.
function Get-LastActivity([datetime]$since) {
	$last = $since.ToUniversalTime()
	foreach ($f in $results, (Join-Path $skseDir 'DovaStash.log')) {
		$item = Get-Item $f -ErrorAction SilentlyContinue
		if ($item -and $item.LastWriteTimeUtc -gt $last) { $last = $item.LastWriteTimeUtc }
	}
	return $last
}

# The id of the last test in results.json ('' before the first).
function Get-LastTestId($doc) {
	$tests = @($doc.tests | Where-Object { $_ })
	if ($tests.Count) { return "$($tests[-1].id)" }
	return ''
}

# Why the game counts as hung, or $null: no write for -StallMinutes since $since, or not responding for 2 minutes.
# $notRespondingSince: when it was first seen not responding ($null while it responds), kept by the caller.
function Test-Stalled($proc, [datetime]$since, [ref]$notRespondingSince) {
	$idle = ((Get-Date).ToUniversalTime() - (Get-LastActivity $since)).TotalMinutes
	$responding = $true
	try { $proc.Refresh(); $responding = $proc.Responding } catch {}
	if ($responding) { $notRespondingSince.Value = $null } elseif (-not $notRespondingSince.Value) { $notRespondingSince.Value = Get-Date }
	$hung = $notRespondingSince.Value -and ((Get-Date) - $notRespondingSince.Value).TotalMinutes -ge 2
	if ($idle -lt $StallMinutes -and -not $hung) { return $null }
	$after = Get-LastTestId (Read-Results)
	return ("stalled {0:0} min after {1}{2}" -f $idle, $(if ($after) { $after } else { 'the start' }), $(if ($hung) { ' (game not responding)' } else { '' }))
}

# Step 5's summary of one run, to the host and to summary.txt in its folder: which plans (first line: "Plans: all" or
# "Plans: -Only '<regex>'", which tells a full run apart when pruning), finished or why not, every test that did not
# pass (passed ones in the file only, unless -ListAll) and the counts. -CountsOnly: the host gets the header and the
# counts only (a run finished by the watchdog or the next run). Returns the lines (the caller may add to them and save
# again), the results and the counts.
function Write-RunSummary([string]$resultsPath, [string]$runDir, [string]$only, [string]$stopReason, [switch]$CountsOnly) {
	$lines = [Collections.Generic.List[string]]::new()
	$add = { param([string]$text, [bool]$fileOnly) $lines.Add($text); if (-not $fileOnly -or ($ListAll -and -not $CountsOnly)) { Write-Host $text } }
	& $add ("Plans: {0}" -f $(if ($only) { "-Only '$only'" } else { 'all' })) $true
	$doc = $null
	try { $doc = Get-Content $resultsPath -Raw -ErrorAction Stop | ConvertFrom-Json } catch {}
	$out = [pscustomobject]@{ lines = $lines; doc = $doc; blocked = 0; xpass = 0; blockedPlans = 0 }
	if (-not $doc) {
		& $add "No results.json in $runDir" $false
	} else {
		$state = if ($doc.finished) { 'finished' } elseif ($stopReason) { "NOT finished: $stopReason" } else {
			$after = Get-LastTestId $doc
			"NOT finished$(if ($after) { " after $after" })"
		}
		& $add ("{0}, Skyrim {1}: {2} ({3})" -f $doc.plugin, $doc.runtime, $state, $doc.reason) $false
		$blockedPlans = @{}
		foreach ($t in $doc.tests) {
			if ($t.status -eq 'blocked') {
				# One line per plan the run refused (the same note for each of its tests): environment, not a test failure.
				if (-not $blockedPlans.ContainsKey($t.plan)) {
					$blockedPlans[$t.plan] = $true
					& $add ("  [blocked] {0}: {1}" -f $t.plan, $t.note) $CountsOnly.IsPresent
				}
				continue
			}
			$line = "  [{0,-6}] {1,-10} {2}" -f $t.status, $t.id, $t.title
			if ($t.note) { $line += "  ($($t.note))" }
			& $add $line ($CountsOnly.IsPresent -or $t.status -eq 'pass')
			if ($t.status -eq 'fail' -or $t.status -eq 'xfail' -or $t.status -eq 'xpass') {
				$label = if ($t.status -eq 'xfail') { 'xfail' } else { 'FAIL' }
				foreach ($c in $t.checks | Where-Object { -not $_.ok }) { & $add "             $label $($c.what): $($c.detail)" $CountsOnly.IsPresent }
			}
		}
		$out.blocked = if ($doc.summary.blocked) { $doc.summary.blocked } else { 0 }
		$out.xpass = if ($doc.summary.xpass) { $doc.summary.xpass } else { 0 }
		$out.blockedPlans = $blockedPlans.Count
		$xfail = if ($doc.summary.xfail) { $doc.summary.xfail } else { 0 }
		& $add ("pass {0}, fail {1}, xpass {2}, xfail {3}, blocked {4} ({5} plan(s)), skip {6}, manual {7}" -f $doc.summary.pass, $doc.summary.fail, $out.xpass, $xfail, $out.blocked, $out.blockedPlans, $doc.summary.skip, $doc.summary.manual) $false
	}
	try { $lines | Set-Content (Join-Path $runDir 'summary.txt') -Encoding utf8NoBOM } catch { Write-Warning "summary.txt could not be written: $_" }
	return $out
}

# Step 5 for a run whose runner was killed before it (running.json still there). The caller holds the lock.
function Complete-StaleRun {
	$stale = Get-Content $statePath -Raw | ConvertFrom-Json -AsHashtable
	Write-Warning "The run started $($stale.started) did not clean up; doing it now ($($stale.runDir))"
	$staleBefore = if ($stale.before) { $stale.before } else { @{} }
	Save-RunLogs $stale.runDir ([datetime]$stale.started) | Out-Null
	$staleResults = Join-Path $stale.runDir 'results.json'
	if (Test-Path $staleResults) {
		Write-RunSummary $staleResults $stale.runDir "$($stale.only)" "$($stale.stopReason)" -CountsOnly | Out-Null
		try { Add-RunHistory $stale.runDir ([datetime]$stale.started).ToUniversalTime().Ticks } catch { Write-Warning "$historyPath could not be written: $_" }
	}
	Complete-Overwrite $staleBefore $stale.runDir @($stale.quarantined)
	Restore-Profile $stale.selected
	Remove-Item -Force $statePath
}

# --- -Finish: only the cleanup of a run whose runner is gone ---------------------------------------------------
if ($Finish) {
	# $until is only the outer bound: once the runner is gone, the game the run started is ended when the run's time
	# (deadline) is up or when it hangs (the runner's stall rule).
	$until = (Get-Date).AddMinutes($WaitMinutes)
	$said = ''
	$notResponding = $null
	while (Test-Path $statePath) {
		try { $stale = Get-Content $statePath -Raw | ConvertFrom-Json -AsHashtable } catch { Start-Sleep -Seconds 2; continue }
		$runner = Test-Alive $stale.pid $stale.startedTicks
		$game = Test-Alive $stale.gamePid $stale.gameTicks
		if (-not $runner -and $game -and (Get-Date) -lt $until) {
			$why = $null
			if ($stale.deadline -and (Get-Date).ToUniversalTime().Ticks -ge [long]$stale.deadline) {
				$why = 'timeout {0} min' -f $(if ($stale.timeoutMinutes) { $stale.timeoutMinutes } else { '?' })
			} elseif ($stale.launched) {
				$proc = Get-Process -Id $stale.gamePid -ErrorAction SilentlyContinue
				if ($proc) { $why = Test-Stalled $proc ([datetime]::new([long]$stale.launched, [DateTimeKind]::Utc)) ([ref]$notResponding) }
			}
			if ($why) {
				Write-Warning "Ending the game the run started (pid $($stale.gamePid)): $why"
				Update-State @{ stopReason = $why }
				Stop-Process -Id $stale.gamePid -Force -ErrorAction SilentlyContinue
				Start-Sleep -Seconds 2
				continue
			}
		}
		if (($runner -or $game) -and (Get-Date) -lt $until) {
			$what = if ($runner) { "the runner (pid $($stale.pid))" } else { "the game the run started (pid $($stale.gamePid))" }
			if ($said -ne $what) { Write-Host "Waiting for $what to exit (until $($until.ToString('HH:mm')))"; $said = $what }
			Start-Sleep -Seconds 15
			continue
		}
		if ($runner) { Stop-Run "The run started $($stale.started) is still running (pid $($stale.pid)); it cleans up itself." 2 }
		if ($game) {
			Write-Warning "Ending the game the run started (pid $($stale.gamePid))"
			Stop-Process -Id $stale.gamePid -Force -ErrorAction SilentlyContinue
		}
		# The MO2 the run started writes its selected_profile when it exits: wait for it as the runner does.
		$mo2Until = (Get-Date).AddMinutes(5)
		while ((Test-Alive $stale.mo2Pid $stale.mo2Ticks) -and (Get-Date) -lt $mo2Until) { Start-Sleep -Seconds 2 }
		$lock = Open-Lock
		if (-not $lock) {
			# Another runner has it: it finishes this run itself when it starts.
			if ((Get-Date) -ge $until) { Stop-Run 'Another run.ps1 is running; it finishes the stopped run itself.' 2 }
			Start-Sleep -Seconds 15
			continue
		}
		try {
			if (Test-Path $statePath) {
				Complete-StaleRun
				Start-Sleep -Seconds 3
				Restore-Profile $stale.selected  # once more, in case MO2 wrote late
			}
		} finally { $lock.Dispose() }
	}
	Write-Host 'No stopped run left to finish.'
	exit 0
}

# --- one run at a time -----------------------------------------------------------------------------------------
$waitUntil = (Get-Date).AddMinutes($WaitMinutes)
while ($true) {
	$lock = Open-Lock
	$busy = if ($lock) { Get-Busy } else { 'another run.ps1 is running' }
	if (-not $busy) { break }
	if ($lock) { $lock.Dispose(); $lock = $null }
	if ((Get-Date) -ge $waitUntil) {
		Stop-Run "Busy: $busy. -WaitMinutes <n> waits for it." 2
	}
	Write-Host "Busy: $busy; checking again in 60 s (until $($waitUntil.ToString('HH:mm')))"
	Start-Sleep -Seconds 60
}
# $lock stays open (FileShare None) until this process ends.

# A run that was stopped before its cleanup: finish it now (its game is gone, or this run would be busy).
if (Test-Path $statePath) {
	$stale = Get-Content $statePath -Raw | ConvertFrom-Json -AsHashtable
	if (Test-Alive $stale.pid $stale.startedTicks) { Stop-Run "The run started $($stale.started) (pid $($stale.pid)) is still running." 2 }
	Complete-StaleRun
}

$started = Get-Date
$runDir = Join-Path $OutDir ($started.ToString('yyyyMMdd-HHmmss'))

# Files from earlier runs (or the player's own setups) in MO2's overwrite folder change what the plans see.
# An earlier run's files go to that run's folder and stay there: every MO2 profile sees overwrite, the player's too.
$leftovers = @(Get-Leftovers)
if ($leftovers.Count) {
	$windows = @(Get-RunWindows)
	$removed = @()
	$userFiles = @()
	foreach ($f in $leftovers) {
		$earlier = Get-EarlierRun $f $windows
		if ($null -eq $earlier) { $userFiles += $f; continue }
		$relative = $f.FullName.Substring($overwrite.Length + 1)
		$dest = if ($earlier) { Join-Path $earlier "written\$relative" } else { Join-Path $runDir "earlier\$relative" }
		if (Test-Path $dest) { $dest = "$dest.$($started.ToString('yyyyMMdd-HHmmss'))" }
		New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
		Move-Item $f.FullName $dest
		$folder = if ($earlier) { "$(Split-Path -Leaf $earlier)\written" } else { "$(Split-Path -Leaf $runDir)\earlier" }
		$removed += "$($f.FullName.Substring($dovaOverwrite.Length + 1)) -> $folder"
	}
	if ($removed.Count) {
		Write-Host "Moved $($removed.Count) file(s) earlier runs left in overwrite into their run folders:"
		$removed | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
		if ($removed.Count -gt 20) { Write-Host "  and $($removed.Count - 20) more" }
	}
	$leftovers = $userFiles
}
$quarantined = @()
if ($leftovers.Count) {
	if ($NoQuarantine) {
		$names = $leftovers | Sort-Object { $_.Extension -eq '.bak' }, FullName | Select-Object -First 6 | ForEach-Object { $_.FullName.Substring($dovaOverwrite.Length + 1) }
		$more = if ($leftovers.Count -gt 6) { ", and $($leftovers.Count - 6) more" } else { '' }
		Stop-Run "overwrite\SKSE\Plugins\DovaStash has $($leftovers.Count) file(s) a run would see: $($names -join ', ')$more. Move them, or run without -NoQuarantine to set them aside for the run." 2
	}
	foreach ($f in $leftovers) {
		$relative = $f.FullName.Substring($overwrite.Length + 1)
		$dest = Join-Path $runDir "preexisting\$relative"
		New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
		Move-Item $f.FullName $dest
		$quarantined += $relative
	}
	Write-Host "Set aside $($quarantined.Count) file(s) from overwrite in $runDir\preexisting (put back after the run)"
}

# What the game writes below Data (layouts, exported templates) lands in MO2's overwrite folder, which every
# profile sees: the files that are new after the run are moved into the run's folder. Files that were there
# before are never touched (the plugin also refuses to run a plan whose home already has a layout).
$overwriteBefore = Get-OverwriteSnapshot
# MO2 remembers the last profile in ModOrganizer.ini: put the player's back afterwards.
$selected = (Get-Content $mo2Ini | Where-Object { $_ -like 'selected_profile=*' } | Select-Object -First 1)
if ($selected -and $selected -ne $ownProfileLine) {
	Set-Content $playerProfilePath $selected -Encoding utf8NoBOM  # for a later run that finds the AutoTest one there
} elseif ($selected -eq $ownProfileLine -and ($PlayerProfile -or -not (Test-Path $playerProfilePath))) {
	$player = Find-PlayerProfile
	if ($player) {
		Set-Content $playerProfilePath (@("selected_profile=@ByteArray($player)") + @(if ($script:profileGuessNote) { 'guessed' })) -Encoding utf8NoBOM
	}
}
# What a later run needs to finish this one if this process is killed before its cleanup.
New-Item -ItemType Directory -Force $runDir | Out-Null
@{
	pid = $PID; started = $started.ToString('o'); startedTicks = (Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks
	runDir = $runDir; selected = $selected; quarantined = $quarantined; before = $overwriteBefore; only = $Only
} | ConvertTo-Json -Depth 3 -Compress | Set-Content $statePath -Encoding utf8NoBOM

# The watchdog (step 6): this script with -Finish, outside this process tree (WMI starts it), so that it outlives a
# shell that ends this runner. It waits while this runner is alive, so its limit covers the whole run: build, three
# starts, the game's timeout (default 60 + 10 min), the MO2 wait.
$watchMinutes = 3 * ((($TimeoutMinutes -gt 0) ? $TimeoutMinutes : 70) + 3) + 30
$pwsh = (Get-Process -Id $PID).Path
$watchArgs = "-NoProfile -NonInteractive -WindowStyle Hidden -File `"$PSCommandPath`" -Finish -WaitMinutes $watchMinutes -StallMinutes $StallMinutes -Mo2 `"$Mo2`" -ProfileName `"$ProfileName`" -OutDir `"$OutDir`""
try {
	$made = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = "`"$pwsh`" $watchArgs" } -ErrorAction Stop
	if ($made.ReturnValue -ne 0) { throw "Win32_Process.Create returned $($made.ReturnValue)" }
} catch {
	try { Start-Process -FilePath $pwsh -ArgumentList $watchArgs -WindowStyle Hidden } catch { Write-Warning "No watchdog ($_): if this runner is killed, run.ps1 -Finish cleans up" }
}

# Everything below cleans up in the finally block (or, if this process is killed, the watchdog or the next run does).
$finished = $false
$dialog = $null
$stopReason = ''  # why the run did not finish: "timeout <n> min", "stalled <n> min after <test id> (game not responding)"
$crash = @()
$launchedAny = $false
try {
	# The game needs Steam; without it MO2 asks (a dialog nobody would answer).
	if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
		$steamExe = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
		if (-not $steamExe) { Stop-Run 'Steam is not running and was not found; start it first.' 2 }
		Write-Host 'Starting Steam (the game needs it)'
		Start-Process -FilePath $steamExe -ArgumentList '-silent'
		$steamUntil = (Get-Date).AddSeconds(120)
		do {
			Start-Sleep -Seconds 3
			$steamPid = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam\ActiveProcess' -ErrorAction SilentlyContinue).pid
			$ready = $steamPid -and (Get-Process -Name steam -ErrorAction SilentlyContinue)
		} until ($ready -or (Get-Date) -ge $steamUntil)
		if (-not $ready) { Stop-Run 'Steam did not finish starting within 2 minutes; start it, then run again.' 2 }
	}

	if ($Build) {
		if (-not $env:VCPKG_ROOT) {
			if (-not (Test-Path 'E:\vcpkg')) { Stop-Run 'Set VCPKG_ROOT to your vcpkg folder (docs/BUILDING.md) for -Build.' 2 }
			Write-Host 'VCPKG_ROOT unset; using E:\vcpkg'
			$env:VCPKG_ROOT = 'E:\vcpkg'
		}
		Push-Location $repo
		try {
			if (-not (Test-Path 'build/plugin/CMakeCache.txt')) {
				Write-Host 'build\plugin is not configured yet: cmake --preset plugin'
				cmake --preset plugin
				if ($LASTEXITCODE -ne 0) { throw 'Configure failed (cmake --preset plugin)' }
			}
			$cache = Get-Content 'build/plugin/CMakeCache.txt'
			$deploy = ($cache | Where-Object { $_ -like 'DOVASTASH_DEPLOY_DIR:*' } | Select-Object -First 1) -replace '^[^=]*=', ''
			$deployOn = ($cache | Where-Object { $_ -like 'DOVASTASH_DEPLOY:*' } | Select-Object -First 1) -replace '^[^=]*=', ''
			Write-Host "Building preset plugin-release (configure preset plugin, build\plugin); deploys to $(if ($deployOn -eq 'OFF') { 'nowhere (DOVASTASH_DEPLOY=OFF)' } elseif ($deploy) { $deploy } else { 'the default folder' })"
			cmake --build --preset plugin-release --target DovaStash --parallel
			if ($LASTEXITCODE -ne 0) { throw 'Build failed (cmake --build --preset plugin-release)' }
		} finally { Pop-Location }
	}

	$profileModlist = Join-Path $Mo2 "profiles\$ProfileName\modlist.txt"
	if (-not (Test-Path $profileModlist)) { Stop-Run "Profile '$ProfileName' missing: run pwsh -File tests\ingame\install.ps1 -Profile" 2 }

	& (Join-Path $repo 'tests\ingame\install.ps1') -Mo2 $Mo2 -ProfileName $ProfileName -Dll $Dll -Only $Only

	# Which DovaStash.dll the profile loads: the highest-priority enabled mod that has one.
	$modlist = Get-Content $profileModlist | Where-Object { $_ -like '+*' } | ForEach-Object { $_.Substring(1) }
	$dllMod = $modlist | Where-Object { Test-Path (Join-Path $Mo2 "mods\$_\SKSE\Plugins\DovaStash.dll") } | Select-Object -First 1
	if (-not $dllMod) { Stop-Run 'No enabled mod in the profile has SKSE\Plugins\DovaStash.dll: build the plugin or pass -Dll' 2 }
	$dllPath = Join-Path $Mo2 "mods\$dllMod\SKSE\Plugins\DovaStash.dll"
	Write-Host ("DovaStash.dll: {0} ({1:yyyy-MM-dd HH:mm})" -f $dllPath, (Get-Item $dllPath).LastWriteTime)

	# The timeout: the plugin's own run timeout (_run.json timeoutMinutes) plus 10 minutes, unless given. A plan
	# takes about a minute (the whole set about 40).
	$plansDir = Join-Path $Mo2 'mods\Dova Stash AutoTest\SKSE\Plugins\DovaStash\AutoTest'
	$planCount = @(Get-ChildItem $plansDir -Filter '*.json' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike '_*' }).Count
	$runTimeout = 60
	try { $runTimeout = [int]((Get-Content (Join-Path $plansDir '_run.json') -Raw | ConvertFrom-Json).timeoutMinutes) } catch {}
	if ($TimeoutMinutes -le 0) { $TimeoutMinutes = $runTimeout + 10 }
	Write-Host "$planCount plan(s): about $planCount min; timeout $TimeoutMinutes min"
	if ($planCount -gt $runTimeout) { Write-Warning "More plans than the $runTimeout-minute run timeout of _run.json may cover: split the run with -Only" }

	# A fresh results folder (the plugin also writes the run's home backups there).
	if (Test-Path $autoDir) {
		# My Games may live in a OneDrive folder: with its cloud provider stopped, placeholder files cannot be removed.
		try { Remove-Item -Recurse -Force $autoDir -ErrorAction Stop } catch {
			Write-Warning "Could not clear $autoDir ($($_.Exception.Message)); removing the results file only"
			Remove-Item -Force $results -ErrorAction SilentlyContinue
		}
	}

	for ($attempt = 1; $attempt -le 3 -and -not $finished -and -not $dialog; $attempt++) {
		$launched = Get-Date
		$launchedAny = $true
		Write-Host "Starting Skyrim with the profile '$ProfileName' ($launched, attempt $attempt)"
		$mo2Process = Start-Process -FilePath $mo2Exe -ArgumentList @('--multiple', '-p', "`"$ProfileName`"", "`"moshortcut://:$Executable`"") -PassThru

		# The game process this run started: the first SkyrimSE.exe that appears after the launch.
		$game = $null
		$deadline = $launched.AddMinutes(3)
		while (-not $game -and (Get-Date) -lt $deadline) {
			Start-Sleep -Seconds 2
			$game = Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $launched } | Select-Object -First 1
		}
		if (-not $game) { Stop-Run 'Skyrim did not start within 3 minutes (see MO2 logs).' 3 }
		Update-State @{ gamePid = $game.Id; gameTicks = $game.StartTime.ToUniversalTime().Ticks; mo2Pid = $mo2Process.Id
			mo2Ticks = $(try { $mo2Process.StartTime.ToUniversalTime().Ticks } catch { 0 })
			# For the watchdog: when this attempt's time is up, and since when it counts writes (the stall rule).
			deadline = (Get-Date).AddMinutes($TimeoutMinutes).ToUniversalTime().Ticks; timeoutMinutes = $TimeoutMinutes
			launched = $launched.ToUniversalTime().Ticks; stopReason = '' }
		Write-Host "Skyrim is running (pid $($game.Id)); waiting for the results (timeout $TimeoutMinutes min)"
		$focused = $false

		$odd = 0
		$stopReason = ''
		$notResponding = $null
		$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
		try {
			while ((Get-Date) -lt $deadline) {
				Start-Sleep -Seconds 5
				# A message box another mod shows before the main menu (a missing file, a version warning) waits for a click
				# nobody makes: report its text and end the run.
				$game.Refresh()
				$title = $game.MainWindowTitle
				if ($Focus -and -not $focused -and $title -eq 'Skyrim Special Edition') {
					# Some game menus (container, crafting) may only open while the window has the focus.
					Add-Type -Namespace AutoTest -Name Win -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);' -ErrorAction SilentlyContinue
					[AutoTest.Win]::SetForegroundWindow($game.MainWindowHandle) | Out-Null
					$focused = $true
				}
				if ($title -and $title -ne 'Skyrim Special Edition') { $odd++ } else { $odd = 0 }
				if ($odd -ge 3) {
					try {
						Add-Type -AssemblyName UIAutomationClient
						$root = [System.Windows.Automation.AutomationElement]::FromHandle($game.MainWindowHandle)
						$texts = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name }
						$dialog = "$title`n" + ($texts -join "`n")
					} catch { $dialog = $title }
					Write-Warning "The game shows a dialog nobody can answer:`n$dialog"
					break
				}
				$doc = Read-Results
				if ($doc -and $doc.finished) { $finished = $true; break }
				if ($game.HasExited) { break }
				# A hung game would hold the machine until the timeout: end it, and say after which test.
				$stopReason = "$(Test-Stalled $game $launched ([ref]$notResponding))"
				if ($stopReason) { Write-Warning "The game is hung: $stopReason"; break }
			}
			if (-not $finished -and -not $dialog -and -not $stopReason -and -not $game.HasExited) { $stopReason = "timeout $TimeoutMinutes min" }
		} finally {
			# Also when the runner is stopped: never leave the game it started running.
			if (-not $game.HasExited) {
				if ($finished) {
					# The game quits by itself after writing the results; give it a moment.
					$game.WaitForExit(60000) | Out-Null
				}
				if (-not $game.HasExited) {
					Write-Warning "Ending the game this run started (pid $($game.Id))"
					Stop-Process -Id $game.Id -Force
				}
			}
			$mo2Process.WaitForExit(60000) | Out-Null
		}

		# The game gone before the run began (MO2 sometimes ends its virtual file system when skse64_loader exits,
		# before the game is up): start again.
		$doc = Read-Results
		if (-not $finished -and -not $dialog -and (-not $doc -or $doc.reason -eq 'starting')) {
			Write-Warning 'The game ended before the tests began; starting it again'
			Start-Sleep -Seconds 10
			continue
		}
		break
	}
} finally {
	# --- collect (also when the run is stopped or fails) ----------------------------------------------------------
	# The MO2 this run started writes its selected_profile when it exits: wait for that (up to 5 min), then put the
	# player's back, and once more a moment later in case it wrote late.
	if ($mo2Process) {
		$mo2Until = (Get-Date).AddMinutes(5)
		while (-not $mo2Process.HasExited -and (Get-Date) -lt $mo2Until) { Start-Sleep -Seconds 2 }
		if (-not $mo2Process.HasExited) { Write-Warning "The MO2 this run started (pid $($mo2Process.Id)) is still running; restoring the profile anyway" }
	}
	Restore-Profile $selected
	if ($mo2Process) {
		Start-Sleep -Seconds 3
		Restore-Profile $selected
	}
	if ($launchedAny) { $crash = Save-RunLogs $runDir $started }
	if ($dialog) { $dialog | Set-Content (Join-Path $runDir 'dialog.txt') }
	if ($stopReason -and $launchedAny) { $stopReason | Set-Content (Join-Path $runDir 'stopped.txt') -Encoding utf8NoBOM }
	if ($launchedAny) {
		try { Add-RunHistory $runDir $started.ToUniversalTime().Ticks } catch { Write-Warning "$historyPath could not be written: $_" }
	}
	Complete-Overwrite $overwriteBefore $runDir $quarantined
	Remove-Item -Force $statePath -ErrorAction SilentlyContinue
}

# --- summary ---------------------------------------------------------------------------------------------------
if (-not (Test-Path $results)) {
	Stop-Run "No results file (the plugin did not start the run: is the AutoTest mod enabled and the DLL current?). Logs: $runDir" 4
}
# The summary goes to the host and to summary.txt in the run folder; passed tests only to the file, unless -ListAll.
Write-Host ''
$summary = Write-RunSummary $results $runDir $Only $stopReason
$doc = $summary.doc
$summaryLines = $summary.lines
$blocked = $summary.blocked
$xpass = $summary.xpass
function Write-Summary([string]$text, [switch]$FileOnly) {
	$summaryLines.Add($text)
	if (-not $FileOnly -or $ListAll) { Write-Host $text }
}
function Save-Summary {
	try { $summaryLines | Set-Content (Join-Path $runDir 'summary.txt') -Encoding utf8NoBOM } catch { Write-Warning "summary.txt could not be written: $_" }
}

# Plans the install left out because the profile lacks a plugin they need: say how to run them.
$sourcePlans = Join-Path $repo 'tests\ingame\mod\SKSE\Plugins\DovaStash\AutoTest'
$unmet = @{}
foreach ($f in Get-ChildItem $sourcePlans -Filter '*.json' -ErrorAction SilentlyContinue) {
	if ($f.Name -like '_*' -or ($Only -and $f.BaseName -notmatch $Only)) { continue }
	if (Test-Path (Join-Path $plansDir $f.Name)) { continue }
	try { $plan = Get-Content $f.FullName -Raw | ConvertFrom-Json } catch { continue }
	foreach ($need in @($plan.requires | Where-Object { $_ })) {
		$list = Get-ChildItem (Join-Path $repo 'tests\ingame\profile') -Filter 'extra-mods*.txt' |
			Where-Object { (Get-Content $_.FullName -Raw).ToLower().Contains($need.ToLower()) } | Select-Object -First 1
		$key = if ($list) { $list.FullName.Substring("$repo".Length + 1) } else { '' }
		if (-not $unmet.ContainsKey($key)) { $unmet[$key] = @() }
		$unmet[$key] += $f.BaseName
		break
	}
}
$notRun = 0
$extraFailed = $false
$extraRuns = @()
foreach ($key in $unmet.Keys) {
	$names = @($unmet[$key] | Sort-Object -Unique)
	if (-not $key) {
		$notRun += $names.Count
		foreach ($n in $names) { Write-Summary ("  [blocked] {0}.json: not run, the profile lacks its plugin" -f $n) }
		Write-Summary "Not run: $($names -join ', '); no tests\ingame\profile\extra-mods*.txt lists a mod with their plugin"
		continue
	}
	# Their own profile, next to the usual one: "Dova Stash AutoTest - wynter" for extra-mods-wynter.txt. When it
	# exists, they run in it right after this run (a second game, in this invocation).
	$suffix = [IO.Path]::GetFileNameWithoutExtension($key) -replace '^extra-mods-?', ''
	$extraProfile = if ($suffix) { "$ProfileName - $suffix" } else { "$ProfileName - extra" }
	if (Test-Path (Join-Path $Mo2 "profiles\$extraProfile\modlist.txt")) {
		$extraRuns += [pscustomobject]@{ profile = $extraProfile; only = "^($($names -join '|'))$"; names = $names }
		continue
	}
	$notRun += $names.Count
	foreach ($n in $names) { Write-Summary ("  [blocked] {0}.json: not run, the profile lacks its plugin" -f $n) }
	Write-Summary "Not run (the profile lacks their plugins): $($names -join ', '). Make their profile once; later runs then run them too:"
	Write-Summary "  pwsh -File tests\ingame\install.ps1 -Profile -ProfileName `"$extraProfile`" -ExtraMods $key"
}
Write-Host "Logs and results: $runDir (summary.txt)"
Save-Summary
# The run folders that exist now: the ones the runs below add are theirs (for the combined last line).
$runDirsBefore = @(Get-ChildItem $OutDir -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
foreach ($x in $extraRuns) {
	Write-Host ''
	Write-Host "Running $($x.names -join ', ') in the profile '$($x.profile)' (their plugins)"
	if ($lock) { $lock.Dispose(); $lock = $null }  # the second run takes the runner lock itself
	$childArgs = @('-NoProfile', '-File', $PSCommandPath, '-Mo2', $Mo2, '-ProfileName', $x.profile, '-Only', $x.only, '-WaitMinutes', '10', '-AllowNotRun')
	if ($Dll) { $childArgs += @('-Dll', $Dll) }
	if ($OutDir) { $childArgs += @('-OutDir', $OutDir) }
	& pwsh @childArgs
	if ($LASTEXITCODE -ne 0) { $extraFailed = $true; Write-Warning "The run in '$($x.profile)' exited with $LASTEXITCODE" }
}
if ($crash) { Write-Warning "Crash log(s): $($crash.Name -join ', ')" }
if ($notRun -gt 0) {
	Write-Summary ("{0} plan(s) not run{1}" -f $notRun, $(if ($AllowNotRun) { ' (-AllowNotRun)' } else { ': the run is not all green without them (-AllowNotRun accepts that)' }))
}
$exitCode = if (-not $doc.finished -or $doc.summary.fail -gt 0 -or $xpass -gt 0 -or $blocked -gt 0 -or $extraFailed -or ($notRun -gt 0 -and -not $AllowNotRun)) { 1 } else { 0 }
if ($extraRuns.Count -gt 0) {
	# One verdict for the whole invocation, last: this run's counts and those of the runs it started.
	$all = [ordered]@{ pass = 0; fail = 0; xpass = 0; blocked = 0; skip = 0; manual = 0 }
	$docs = @($doc)
	foreach ($d in Get-ChildItem $OutDir -Directory -ErrorAction SilentlyContinue | Where-Object { $runDirsBefore -notcontains $_.FullName }) {
		$r = Join-Path $d.FullName 'results.json'
		if (Test-Path $r) { try { $docs += Get-Content $r -Raw | ConvertFrom-Json } catch { Write-Warning "$r could not be read: $_" } }
	}
	foreach ($d in $docs) { foreach ($k in @($all.Keys)) { if ($d.summary.$k) { $all[$k] += [int]$d.summary.$k } } }
	Write-Summary ("all: pass {0}, fail {1}, xpass {2}, blocked {3}, skip {4}, manual {5}; exit {6}" -f $all.pass, $all.fail, $all.xpass, $all.blocked, $all.skip, $all.manual, $exitCode)
}
Save-Summary

# Keeps the newest 20 run folders, every one younger than 48 hours, and the newest run of every plan (summary.txt's
# first line "Plans: all"), so a full run's evidence outlives a day of -Only runs. An older one goes, unless its
# preexisting\ still holds the player's files (a run that could not put them back) or it is the run a stopped runner
# left to finish (running.json).
try {
	$pending = if (Test-Path $statePath) { "$((Get-Content $statePath -Raw | ConvertFrom-Json).runDir)" } else { '' }
	$runFolders = @(Get-ChildItem $OutDir -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^\d{8}-\d{6}$' } | Sort-Object Name -Descending)
	$fullRun = $runFolders | Where-Object {
		$s = Join-Path $_.FullName 'summary.txt'
		(Test-Path $s) -and ((Get-Content $s -TotalCount 1) -eq 'Plans: all')
	} | Select-Object -First 1
	foreach ($d in $runFolders | Select-Object -Skip 20) {
		if ($d.FullName -eq $runDir -or $d.FullName -eq $pending -or ($fullRun -and $d.FullName -eq $fullRun.FullName)) { continue }
		$made = [datetime]::MinValue
		if ([datetime]::TryParseExact($d.Name, 'yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture, 'AssumeLocal', [ref]$made) -and
			$made -gt (Get-Date).AddHours(-48)) { continue }
		$pre = Join-Path $d.FullName 'preexisting'
		if ((Test-Path $pre) -and (Get-ChildItem $pre -Recurse -File -Force -ErrorAction SilentlyContinue)) { continue }
		Remove-Item -Recurse -Force $d.FullName -ErrorAction SilentlyContinue
	}
} catch { Write-Warning "Old run folders could not be pruned: $_" }
exit $exitCode
