<#
.SYNOPSIS
  Runs the automated in-game tests unattended (docs/TESTING.md, "Automated in-game tests").

.DESCRIPTION
  1. Optionally builds the plugin (-Build; the build deploys as configured, see docs/BUILDING.md).
  2. Installs the plans into the MO2 mod "Dova Stash AutoTest" (tests/ingame/install.ps1; -Dll puts a DLL of
     your own into the AutoTest mod for this profile only).
  3. Starts Skyrim through MO2 with the "Dova Stash AutoTest" profile and SKSE:
       ModOrganizer.exe --multiple -p "Dova Stash AutoTest" "moshortcut://:Launch Game"
     (--multiple: a second MO2 process, so an MO2 window that is already open is left alone).
  4. Waits for SKSE/DovaStash/AutoTest/results.json to say finished (the game then quits by itself), or the
     timeout: then it ends the game process it started, and only that one. A dialog another mod shows (nobody
     can click it) ends the run too, with its text.
  5. Copies DovaStash.log, results.json, the Papyrus log and the home backups into a timestamped folder, and moves
     what the run wrote into MO2's overwrite folder (layouts, exported templates: new files only) there too, prints a summary and exits 1 if a test failed or the run
     did not finish.
  Refuses to start while a Skyrim is running.

.EXAMPLE
  pwsh -File tools\autotest\run.ps1
  pwsh -File tools\autotest\run.ps1 -Build
  pwsh -File tools\autotest\run.ps1 -Dll build\plugin\src\plugin\Release\DovaStash.dll
#>
[CmdletBinding()]
param(
	[string]$Mo2 = 'E:\modding\Kexodus Skyrim',
	[string]$ProfileName = 'Dova Stash AutoTest',
	[string]$Executable = 'Launch Game',
	[switch]$Build,
	[string]$Dll = '',
	[int]$TimeoutMinutes = 25,
	[switch]$Focus,
	[string]$OutDir = '',
	[string]$Only = ''
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

if (Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue) {
	Write-Error 'A Skyrim (SkyrimSE.exe) is running: close it first. The test run does not touch a game it did not start.'
	exit 2
}

# The game needs Steam; without it MO2 asks (a dialog nobody would answer).
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) {
	$steamExe = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamExe
	if (-not $steamExe) { Write-Error 'Steam is not running and was not found; start it first.'; exit 2 }
	Write-Host 'Starting Steam (the game needs it)'
	Start-Process -FilePath $steamExe -ArgumentList '-silent'
	Start-Sleep -Seconds 30
}

if ($Build) {
	if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'E:\vcpkg' }
	Push-Location $repo
	try {
		cmake --build build/plugin --config Release --target DovaStash --parallel
		if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
	} finally { Pop-Location }
}

& (Join-Path $repo 'tests\ingame\install.ps1') -Mo2 $Mo2 -Dll $Dll -Only $Only

# Which DovaStash.dll the profile loads: the highest-priority enabled mod that has one.
$modlist = Get-Content (Join-Path $Mo2 "profiles\$ProfileName\modlist.txt") | Where-Object { $_ -like '+*' } | ForEach-Object { $_.Substring(1) }
$dllMod = $modlist | Where-Object { Test-Path (Join-Path $Mo2 "mods\$_\SKSE\Plugins\DovaStash.dll") } | Select-Object -First 1
$dllPath = Join-Path $Mo2 "mods\$dllMod\SKSE\Plugins\DovaStash.dll"
Write-Host ("DovaStash.dll: {0} ({1:yyyy-MM-dd HH:mm})" -f $dllPath, (Get-Item $dllPath).LastWriteTime)

# What the game writes below Data (layouts, exported templates) lands in MO2's overwrite folder, which every
# profile sees: the files that are new after the run are moved into the run's folder. Files that were there before
# are never touched (the plugin also refuses to run a plan whose home already has a layout).
$overwrite = Join-Path $Mo2 'overwrite'
$overwriteBefore = @{}
Get-ChildItem $overwrite -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $overwriteBefore[$_.FullName] = $_.LastWriteTimeUtc }

# A fresh results folder (the plugin also writes the run's home backups there).
if (Test-Path $autoDir) {
	# My Games may live in a OneDrive folder: with its cloud provider stopped, placeholder files cannot be removed.
	try { Remove-Item -Recurse -Force $autoDir -ErrorAction Stop } catch {
		Write-Warning "Could not clear $autoDir ($($_.Exception.Message)); removing the results file only"
		Remove-Item -Force $results -ErrorAction SilentlyContinue
	}
}

# MO2 remembers the last profile in ModOrganizer.ini: put the player's back afterwards.
$selected = (Get-Content $mo2Ini | Where-Object { $_ -like 'selected_profile=*' } | Select-Object -First 1)

function Read-Results {
	if (-not (Test-Path $results)) { return $null }
	try {
		# Read without locking: the game renames its new results over this file.
		$stream = [IO.File]::Open($results, 'Open', 'Read', 'ReadWrite, Delete')
		try { $text = (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
		return $text | ConvertFrom-Json
	} catch { return $null }
}

$started = Get-Date
$finished = $false
$dialog = $null
for ($attempt = 1; $attempt -le 3 -and -not $finished -and -not $dialog; $attempt++) {
	$launched = Get-Date
	Write-Host "Starting Skyrim with the profile '$ProfileName' ($launched, attempt $attempt)"
	$mo2Process = Start-Process -FilePath $mo2Exe -ArgumentList @('--multiple', '-p', "`"$ProfileName`"", "`"moshortcut://:$Executable`"") -PassThru

	# The game process this run started: the first SkyrimSE.exe that appears after the launch.
	$game = $null
	$deadline = $launched.AddMinutes(3)
	while (-not $game -and (Get-Date) -lt $deadline) {
		Start-Sleep -Seconds 2
		$game = Get-Process -Name SkyrimSE -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $launched } | Select-Object -First 1
	}
	if (-not $game) {
		Write-Error 'Skyrim did not start within 3 minutes (see MO2 logs).'
		exit 3
	}
	Write-Host "Skyrim is running (pid $($game.Id)); waiting for the results (timeout $TimeoutMinutes min)"
	$focused = $false

	$odd = 0
	$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
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
	}

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

if ($selected) {
	$ini = Get-Content $mo2Ini
	$now = $ini | Where-Object { $_ -like 'selected_profile=*' } | Select-Object -First 1
	if ($now -ne $selected) {
		($ini | ForEach-Object { if ($_ -like 'selected_profile=*') { $selected } else { $_ } }) | Set-Content $mo2Ini -Encoding utf8
		Write-Host "ModOrganizer.ini: $selected restored"
	}
}

# --- collect ---------------------------------------------------------------------------------------------------
$runDir = Join-Path $OutDir ($started.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $runDir | Out-Null
foreach ($f in (Join-Path $skseDir 'DovaStash.log'), $results, (Join-Path $myGames 'Logs\Script\Papyrus.0.log')) {
	if (Test-Path $f) { Copy-Item -Force $f $runDir }
}
if (Test-Path (Join-Path $autoDir 'Backups')) { Copy-Item -Recurse -Force (Join-Path $autoDir 'Backups') (Join-Path $runDir 'Backups') }
$written = Join-Path $runDir 'written'
foreach ($file in Get-ChildItem $overwrite -Recurse -File -Force -ErrorAction SilentlyContinue) {
	$relative = $file.FullName.Substring($overwrite.Length + 1)
	if (-not $overwriteBefore.ContainsKey($file.FullName)) {
		$dest = Join-Path $written $relative
		New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
		Move-Item -Force $file.FullName $dest
	} elseif ($overwriteBefore[$file.FullName] -ne $file.LastWriteTimeUtc) {
		Write-Warning "overwrite\$relative was there before and changed during the run (left as it is)"
	}
}
$crash = Get-ChildItem $skseDir -Filter 'crash-*.log' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started }
foreach ($c in $crash) { Copy-Item -Force $c.FullName $runDir }
if ($dialog) { $dialog | Set-Content (Join-Path $runDir 'dialog.txt') }

# --- summary ---------------------------------------------------------------------------------------------------
if (-not (Test-Path $results)) {
	Write-Error "No results file (the plugin did not start the run: is the AutoTest mod enabled and the DLL current?). Logs: $runDir"
	exit 4
}
$doc = Get-Content $results -Raw | ConvertFrom-Json
Write-Host ''
Write-Host ("{0}, Skyrim {1}: {2} ({3})" -f $doc.plugin, $doc.runtime, $(if ($doc.finished) { 'finished' } else { 'NOT finished' }), $doc.reason)
foreach ($t in $doc.tests) {
	$line = "  [{0,-6}] {1,-10} {2}" -f $t.status, $t.id, $t.title
	if ($t.note) { $line += "  ($($t.note))" }
	Write-Host $line
	if ($t.status -eq 'fail') {
		foreach ($c in $t.checks | Where-Object { -not $_.ok }) { Write-Host "             FAIL $($c.what): $($c.detail)" }
	}
}
Write-Host ("pass {0}, fail {1}, skip {2}, manual {3}" -f $doc.summary.pass, $doc.summary.fail, $doc.summary.skip, $doc.summary.manual)
Write-Host "Logs and results: $runDir"
if ($crash) { Write-Warning "Crash log(s): $($crash.Name -join ', ')" }
if (-not $doc.finished -or $doc.summary.fail -gt 0) { exit 1 }
exit 0
