<#
.SYNOPSIS
  Installs the automated in-game tests into an MO2 instance (docs/TESTING.md, "Automated in-game tests").

.DESCRIPTION
  - Mirrors tests/ingame/mod into the mod "Dova Stash AutoTest" (the plans and the test INI). Nothing else in
    the instance changes.
  - With -Profile: (re)creates the MO2 profile "Dova Stash AutoTest": base game, DLCs and Creation Club as installed,
    the Unofficial Patches, SKSE, Address Library, SKSE Menu Framework, UIExtensions, QuickLoot IE (and SkyUI, which its loot menu needs), KID, Dova Stash
    and the two AutoTest mods, nothing else; profile-local INIs for unattended runs (tests/ingame/profile).
    The Creation Club and DLC entries are copied from -SourceProfile. No other profile is changed.
  - With -Only <regex>: only the plans whose file name matches are installed (plus _run.json), so one run stays
    under the run timeout. Without it, every plan.
  - Extra home mods for the profile: tests/ingame/profile/extra-mods.txt, one "Mod folder name|plugin.esp[,more.esp]"
    per line (# comments; -ExtraMods <file> names another list, e.g. extra-mods-wynter.txt). They are enabled below the base mods, their plugins after the base plugins, so plans
    for mod-added homes can run. A missing mod is skipped with a warning (the plans for it then fail on `enter`).
  - With -Dll <path>: that DovaStash.dll (and its .pdb) goes into the AutoTest mod, overriding the Dova Stash mod's
    in the AutoTest profile only (a build of a branch, without touching the player's mod folder). Without -Dll, a
    DLL left there by an earlier run is removed, so the profile tests the Dova Stash mod's own DLL.

.EXAMPLE
  powershell -File tests\ingame\install.ps1 -Profile
#>
[CmdletBinding()]
param(
	[string]$Mo2 = 'E:\modding\Kexodus Skyrim',
	[string]$ProfileName = 'Dova Stash AutoTest',
	[switch]$Profile,
	[string]$SourceProfile = 'Dova Stash Testing',
	[string]$Dll = '',
	[string]$Only = '',
	[string]$ExtraMods = ''
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$modsDir = Join-Path $Mo2 'mods'
$testMod = Join-Path $modsDir 'Dova Stash AutoTest'

function Write-Meta([string]$dir, [string]$comment) {
	$meta = Join-Path $dir 'meta.ini'
	if (-not (Test-Path $meta)) {
		@"
[General]
gameName=SkyrimSE
modid=0
version=1.0.0
installationFile=
comments=$comment
"@ | Set-Content -Path $meta -Encoding ascii
	}
}

# --- the AutoTest mod: plans + test INI ----------------------------------------------------------------------
New-Item -ItemType Directory -Force $testMod | Out-Null
$plans = Join-Path $testMod 'SKSE\Plugins\DovaStash\AutoTest'
if (Test-Path $plans) { Remove-Item -Recurse -Force $plans }
Copy-Item -Recurse -Force (Join-Path $here 'mod\*') $testMod
if ($Only) {
	$kept = 0
	foreach ($f in Get-ChildItem $plans -Filter '*.json') {
		if ($f.Name -like '_*') { continue }
		if ($f.BaseName -match $Only) { $kept++ } else { Remove-Item -Force $f.FullName }
	}
	Write-Host "AutoTest: -Only '$Only' keeps $kept plan(s)"
	if ($kept -eq 0) { throw "No plan matches -Only '$Only'" }
}
Write-Meta $testMod 'Dova Stash automated in-game tests (tests/ingame). Enable only in the Dova Stash AutoTest profile.'

# Test templates (mod/SKSE/Plugins/DovaStash/Templates/AutoTest/*.json) are seen by every plan of the run, and one with a
# higher priority changes what "Use template" picks for its cell: only the templates the installed plans declare
# ("templates": ["LakeviewManor.json"]) are kept, so plans that need the shipped template alone can run without them.
$testTemplates = Join-Path $testMod 'SKSE\Plugins\DovaStash\Templates\AutoTest'
if (Test-Path $testTemplates) {
	$declared = @{}
	foreach ($f in Get-ChildItem $plans -Filter '*.json') {
		if ($f.Name -like '_*') { continue }
		try { $plan = Get-Content $f.FullName -Raw | ConvertFrom-Json } catch { continue }
		foreach ($t in @($plan.templates)) { if ($t) { $declared[$t.ToLower()] = $true } }
	}
	$removed = 0
	foreach ($t in Get-ChildItem $testTemplates -Filter '*.json') {
		if (-not $declared.ContainsKey($t.Name.ToLower())) { Remove-Item -Force $t.FullName; $removed++ }
	}
	Write-Host "AutoTest: $($declared.Count) test template(s) kept, $removed not declared by an installed plan removed"
}

$dllTarget = Join-Path $testMod 'SKSE\Plugins\DovaStash.dll'
$pdbTarget = Join-Path $testMod 'SKSE\Plugins\DovaStash.pdb'
if ($Dll) {
	Copy-Item -Force $Dll $dllTarget
	$pdb = [IO.Path]::ChangeExtension($Dll, '.pdb')
	if (Test-Path $pdb) { Copy-Item -Force $pdb $pdbTarget }
	Write-Host "AutoTest: DovaStash.dll override from $Dll"
} else {
	foreach ($f in $dllTarget, $pdbTarget) { if (Test-Path $f) { Remove-Item -Force $f } }
}

Write-Host "AutoTest: installed $testMod"

if (-not $Profile) { return }

# --- the profile ----------------------------------------------------------------------------------------------
$profiles = Join-Path $Mo2 'profiles'
$source = Join-Path $profiles $SourceProfile
$target = Join-Path $profiles $ProfileName
if (-not (Test-Path $source)) { throw "Source profile not found: $source" }

$mods = @(
	'Dova Stash AutoTest',
	'Dova Stash',
	'QuickLoot IE - A QuickLoot EE Fork',
	'SkyUI',
	'SKSE Menu Framework',
	'UIExtensions',
	'Keyword Item Distributor',
	'Unofficial Skyrim Creation Club Content Patch',
	'Unofficial Skyrim Special Edition Patch - USSEP',
	'Address Library for SKSE Plugins',
	'SKSE'
)
foreach ($m in $mods) {
	if (-not (Test-Path (Join-Path $modsDir $m))) { throw "Mod not installed: $m" }
}

# Extra home mods (tests/ingame/profile/extra-mods.txt): "Mod folder|plugin.esp[,plugin2.esp]" per line.
$homeMods = @()
$extraPlugins = @()
$extraFile = if ($ExtraMods) { $ExtraMods } else { Join-Path $here 'profile\extra-mods.txt' }
if (Test-Path $extraFile) {
	foreach ($line in Get-Content $extraFile) {
		$line = $line.Trim()
		if (-not $line -or $line.StartsWith('#')) { continue }
		$name, $plugs = $line.Split('|', 2)
		$name = $name.Trim()
		if (-not (Test-Path (Join-Path $modsDir $name))) { Write-Warning "extra mod not installed, skipped: $name"; continue }
		$homeMods += $name
		if ($plugs) { $extraPlugins += ($plugs.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
	}
}

New-Item -ItemType Directory -Force $target, (Join-Path $target 'saves') | Out-Null
$header = '# This file was automatically generated by Mod Organizer.'

# modlist.txt: highest priority first; DLC and Creation Club ("*" entries) as the source profile has them.
$unmanaged = Get-Content (Join-Path $source 'modlist.txt') | Where-Object { $_ -like '`**' }
$modlist = @($header) + ($mods | ForEach-Object { "+$_" }) + ($homeMods | ForEach-Object { "+$_" }) + $unmanaged
$modlist | Set-Content -Path (Join-Path $target 'modlist.txt') -Encoding utf8

# Plugins: the masters and Creation Club files as the source load order has them, then the mods' plugins.
$plugins = @('Unofficial Skyrim Special Edition Patch.esp', 'unofficial skyrim creation club content patch.esl', 'SkyUI_SE.esp', 'UIExtensions.esp', 'QuickLootIE.esp') + $extraPlugins
$base = Get-Content (Join-Path $source 'loadorder.txt') | Where-Object {
	$_ -match '^(Skyrim|Update|Dawnguard|HearthFires|Dragonborn)\.esm$' -or $_ -match '^cc\w+.*\.es[ml]$' -or $_ -eq '_ResourcePack.esl'
}
(@($header) + $base + $plugins) | Set-Content -Path (Join-Path $target 'loadorder.txt') -Encoding utf8
(@($header) + ($plugins | ForEach-Object { "*$_" })) | Set-Content -Path (Join-Path $target 'plugins.txt') -Encoding utf8
$header | Set-Content -Path (Join-Path $target 'lockedorder.txt') -Encoding utf8

foreach ($f in 'settings.ini', 'skyrim.ini', 'skyrimprefs.ini', 'skyrimcustom.ini', 'initweaks.ini') {
	Copy-Item -Force (Join-Path $here "profile\$f") (Join-Path $target $f)
}
Write-Host "AutoTest: profile $target written ($($mods.Count + $homeMods.Count) mods, $($base.Count + $plugins.Count) plugins)"
