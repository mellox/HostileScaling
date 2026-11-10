# Runs a Satisfactory mod BUILD or release PACKAGE with the WeaponUpgrades plugin
# temporarily disabled.
#
# Why: PackagePlugin compiles ALL mods discovered in the starter project, and
# WeaponUpgrades' AccessTransformer friend entry trips a UHT "unused" false-flag
# that fails the shared target with C2248 (see _team/playbooks/satisfactory-mod.md).
# So any build/package of a mod that ISN'T WeaponUpgrades must hide its .uplugin
# first and restore it afterwards, even when the run fails.
#
# Usage:
#   .\build_wu_disabled.ps1                       # dev build + deploy (default)
#   .\build_wu_disabled.ps1 -Package              # release multi-target zip for ficsit
#   .\build_wu_disabled.ps1 -ModName HostileScaling -Package
#   .\build_wu_disabled.ps1 -VerifyOnly           # post-build gates only, against what is deployed now
#   .\build_wu_disabled.ps1 -VerifyOnly -SkipUhtLogGate -ExtraThunk AHostileScalingSubsystem::NoSuchFn  # forced FAIL
#
# Delegates the actual work (game-check, cross-session lock, mirror sync, UAT,
# verification) to the satisfactory-modding skill scripts.
#
# Stale-reflection guard (packet mcp2, 2026-11-10; mechanism in
# _team/PFA-reviews/2026-11-09-uht-stale-shipping-research.md): UBT runs the header tool only when a
# reflected header is newer than the module's UHT Timestamp, and ANY mod's build of a target re-stamps
# every mod's Timestamp, while the robocopy sync preserves repo mtimes. So:
#  A1 before delegating, this mod's canonical headers get mtime NOW (content untouched); the sync then
#     copies them with that mtime, newer than every target's stamp. Chosen over deleting the stamp
#     because the stamp is shared with other sessions' live builds (any target build rewrites it) and
#     because a missing stamp relaxes the AT pre-gate's own-grant check (at_grants_check.py
#     uht_rerun_reasons); the delete would have to happen before the lock and the pre-gate, which run
#     inside the shared script.
#  A2 after a successful run: this run's UAT log must hold the header-tool lines for every target built.
#  A3 the deployed DLL must carry the class-qualified native-thunk export for every UFUNCTION declared
#     on an exported class in the mod's headers (derived, never hard-coded).
# Exit codes added here: 30 child exit code unreadable, 31 child threw, 32 header-tool log gate,
# 33 thunk gate, 34 header mtime update failed or a stamp not older than it, 35 liveLatencySeconds pin.
# Every other code is the child's.

param(
    [string]$ModName = "HostileScaling",
    [switch]$Package,
    [switch]$VerifyOnly,
    [switch]$SkipUhtLogGate,
    [string[]]$ExtraThunk = @(),
    [string]$SkillScripts = "$env:USERPROFILE\.claude\skills\satisfactory-modding\scripts",
    [string]$WuPlugin = "C:\Claude\Projects\SatisfactoryModLoader\Mods\WeaponUpgrades\WeaponUpgrades.uplugin",
    [string]$SourceRoot = "C:\Claude\Projects",
    [string]$StarterProject = "C:\Claude\Projects\SatisfactoryModLoader",
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\Satisfactory"
)

$ErrorActionPreference = "Stop"

if ($ModName -eq "WeaponUpgrades") {
    Write-Error "WeaponUpgrades builds itself normally - call the skill script directly."
    exit 1
}

# UBT targets each mode compiles (build_mod.ps1 :31-33; package_release.ps1 :43).
$UhtTargets = if ($Package) { @('FactoryGameSteam', 'FactoryGameEGS', 'FactoryServer') } else { @('FactoryGameSteam') }

# ---- A1: make this mod's headers newer than every UHT stamp -----------------------
function Update-HeaderMtimes {
    $root = Join-Path $SourceRoot "$ModName\Source"
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        Write-Host "HSBUILD UHT-FORCE canonicalSource=absent path=$root touched=0"
        return $false
    }
    $now = [DateTime]::UtcNow
    $hdrs = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.h)
    foreach ($h in $hdrs) { [System.IO.File]::SetLastWriteTimeUtc($h.FullName, $now) }
    $after = @($hdrs | Where-Object { (Get-Item -LiteralPath $_.FullName).LastWriteTimeUtc -ne $now }).Count
    Write-Host ("HSBUILD UHT-FORCE root={0} headers={1} setTo={2} notAtSetTime={3}" -f $root, $hdrs.Count, $now.ToString('o'), $after)
    $script:stampAhead = $false
    foreach ($t in $UhtTargets) {
        $stamp = Join-Path $StarterProject "Mods\$ModName\Intermediate\Build\Win64\$t\Inc\$ModName\UHT\Timestamp"
        $sm = if (Test-Path -LiteralPath $stamp) { (Get-Item -LiteralPath $stamp).LastWriteTimeUtc } else { $null }
        Write-Host ("HSBUILD UHT-FORCE target={0} stampMtime={1} headersNewerThanStamp={2}" -f $t,
            $(if ($sm) { $sm.ToString('o') } else { 'absent' }), $(if ($sm) { [bool]($now -gt $sm) } else { 'n/a' }))
        if ($sm -and $sm -ge $now) { $script:stampAhead = $true }
    }
    return ($hdrs.Count -gt 0 -and $after -eq 0 -and -not $script:stampAhead)
}

# ---- A2: header-tool evidence in this run's UAT log --------------------------------
function Test-UhtLog($RunStart) {
    $log = Join-Path $env:TEMP $(if ($Package) { "package_$ModName.log" } else { "alpakit_$ModName.log" })
    if (-not (Test-Path -LiteralPath $log)) { Write-Host "HSBUILD UHT-LOG log=$log exists=False"; return $false }
    $mt = (Get-Item -LiteralPath $log).LastWriteTimeUtc
    if ($null -eq $RunStart) {
        Write-Host ("HSBUILD UHT-LOG log={0} mtime={1} freshness=not-tested (-VerifyOnly has no run start)" -f $log, $mt.ToString('o'))
    } else {
        $fresh = $mt -ge $RunStart
        Write-Host ("HSBUILD UHT-LOG log={0} mtime={1} runStart={2} writtenThisRun={3}" -f $log, $mt.ToString('o'), $RunStart.ToString('o'), $fresh)
        if (-not $fresh) { return $false }
    }
    $lines = @(Get-Content -LiteralPath $log)
    $ok = $true
    foreach ($t in $UhtTargets) {
        foreach ($phrase in @(('Parsing headers' + ' for ' + $t), ('Reflection code' + ' generated for ' + $t))) {
            $rx = '^\s*' + [regex]::Escape($phrase) + '\b'
            $n = @($lines | Where-Object { $_ -match $rx }).Count
            Write-Host "HSBUILD UHT-LOG target=$t phrase='$phrase' lineInitialHits=$n"
            if ($n -lt 1 -and $Package -and $t -ne 'FactoryGameSteam') {
                Write-Host "HSBUILD UHT-LOG PACKAGE target=$t evidence absent; the zip may be good - rely on the THUNK lines for this target"
            } elseif ($n -lt 1) { $ok = $false }
        }
    }
    return $ok
}

# ---- A3: native-thunk exports in the deployed DLL(s) --------------------------------
# Derived per module from the Mods copy UBT compiled. Only classes declared with the module's
# export macro are provable (the thunk name reaches the DLL through the export table).
function Get-DerivedUFunctions([string]$ModuleDir, [string]$Api) {
    $found = [System.Collections.Generic.List[object]]::new()
    $macro = 'UFUNC' + 'TION('
    $raw = 0
    foreach ($f in @(Get-ChildItem -LiteralPath $ModuleDir -Recurse -File -Filter *.h)) {
        $t = [System.IO.File]::ReadAllText($f.FullName) -replace "`r`n", "`n"
        $t = [regex]::Replace($t, '/\*.*?\*/', ' ', 'Singleline')
        $t = [regex]::Replace($t, '//[^\n]*', '')
        $all = [regex]::Matches($t, [regex]::Escape($macro)).Count
        $lead = [regex]::Matches($t, '(?m)^[ \t]*' + [regex]::Escape($macro)).Count
        if ($all -ne $lead) { $found.Add([pscustomobject]@{ Class = $null; Fn = $null; File = $f.Name; Reason = "$all macro site(s), $lead line-initial" }) }
        foreach ($m in [regex]::Matches($t, '(?m)^[ \t]*' + [regex]::Escape($macro))) {
            $raw++
            $i = $m.Index + $m.Length; $depth = 1
            while ($i -lt $t.Length -and $depth -gt 0) { if ($t[$i] -eq '(') { $depth++ } elseif ($t[$i] -eq ')') { $depth-- }; $i++ }
            $spec = $t.Substring($m.Index + $m.Length, [Math]::Max(0, $i - 1 - ($m.Index + $m.Length)))
            if ($spec -match '\bBlueprintImplementableEvent\b') { continue }
            $rest = $t.Substring($i); $p = $rest.IndexOf('(')
            $id = if ($p -ge 0) { [regex]::Match($rest.Substring(0, $p), '([A-Za-z_][A-Za-z0-9_]*)\s*$') } else { $null }
            $decls = [regex]::Matches($t.Substring(0, $m.Index), '(?m)^[ \t]*(?:class|struct)[ \t]+(' + $Api + '[ \t]+)?([A-Za-z_][A-Za-z0-9_]*)[^;\n]*$')
            if (-not $id -or -not $id.Success -or $decls.Count -eq 0) { $found.Add([pscustomobject]@{ Class = $null; Fn = $null; File = $f.Name }); continue }
            $o = $decls[$decls.Count - 1]
            $found.Add([pscustomobject]@{ Class = $o.Groups[2].Value; Fn = $id.Groups[1].Value; Exported = $o.Groups[1].Success; File = $f.Name })
        }
    }
    return [pscustomobject]@{ Raw = $raw; Items = $found }
}

function Get-Count([string]$Text, [string]$Needle) {
    $n = 0; $at = 0
    while (($at = $Text.IndexOf($Needle, $at, [System.StringComparison]::Ordinal)) -ge 0) { $n++; $at += $Needle.Length }
    return $n
}

function Test-Thunks($RunStart) {
    $src = Join-Path $StarterProject "Mods\$ModName\Source"
    if (-not (Test-Path -LiteralPath $src -PathType Container)) { Write-Host "HSBUILD THUNK sourceRoot=$src exists=False"; return $false }
    $ok = $true; $checked = 0
    foreach ($mod in @(Get-ChildItem -LiteralPath $src -Directory | Where-Object { Test-Path (Join-Path $_.FullName "$($_.Name).Build.cs") })) {
        $api = $mod.Name.ToUpperInvariant() + '_' + 'API'
        $d = Get-DerivedUFunctions $mod.FullName $api
        $items = @($d.Items)
        foreach ($bad in @($items | Where-Object { -not $_.Fn })) { Write-Host "HSBUILD THUNK FAIL derive: $(if ($bad.Reason) { $bad.Reason } else { 'no function name or owning class after a UFUNCTION' }) in $($bad.File)"; $ok = $false }
        foreach ($u in @($items | Where-Object { $_.Fn -and -not $_.Exported })) { Write-Host "HSBUILD THUNK NOT-PROVABLE $($u.Class)::$($u.Fn) (class declared without $api; not checked, not passed)" }
        $want = @($items | Where-Object { $_.Fn -and $_.Exported } | ForEach-Object { "$($_.Class)::$($_.Fn)" })
        if ($mod.Name -eq $ModName) { $want += @($ExtraThunk | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
        $want = @($want | Select-Object -Unique)
        Write-Host ("HSBUILD THUNK module={0} headerMacroSites={1} checkable={2}" -f $mod.Name, $d.Raw, $want.Count)
        if ($mod.Name -eq $ModName -and $d.Raw -gt 0 -and $want.Count -eq 0) { Write-Host "HSBUILD THUNK FAIL $($mod.Name) has UFUNCTION sites but none checkable"; $ok = $false }
        if ($want.Count -eq 0) { continue }
        $dlls = @(Join-Path $GameDir "FactoryGame\Mods\$ModName\Binaries\Win64\FactoryGameSteam-$($mod.Name)-Win64-Shipping.dll")
        if ($Package) { foreach ($t in @('FactoryGameEGS','FactoryServer')) { $dlls += Join-Path $StarterProject "Mods\$ModName\Binaries\Win64\$t-$($mod.Name)-Win64-Shipping.dll" } }
        foreach ($dll in $dlls) {
        if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { Write-Host "HSBUILD THUNK FAIL dll=$dll exists=False"; $ok = $false; continue }
        $bytes = [System.IO.File]::ReadAllBytes($dll)
        $text = [System.Text.Encoding]::Latin1.GetString($bytes)   # 1 byte -> 1 char, alignment-agnostic
        $dllTime = (Get-Item -LiteralPath $dll).LastWriteTimeUtc
        $fresh = if ($null -eq $RunStart) { 'not-tested' } else { [bool]($dllTime -ge $RunStart) }
        Write-Host ("HSBUILD THUNK dll={0} bytes={1} mtime={2} writtenThisRun={3}" -f $dll, $bytes.Length, $dllTime.ToString('o'), $fresh)
        if ($fresh -eq $false) { Write-Host "HSBUILD THUNK FAIL dll not written by this run"; $ok = $false }
        foreach ($cls in @($want | ForEach-Object { ($_ -split '::')[0] } | Select-Object -Unique)) {
            $pos = Get-Count $text ('?' + 'Static' + 'Class@' + $cls + '@@')
            $neg = Get-Count $text ('?' + 'ex' + 'ec' + 'HsAbsent' + 'Control' + '@' + $cls + '@@')
            Write-Host "HSBUILD THUNK control class=$cls staticClassExport=$pos absentNameExport=$neg"
            if ($pos -lt 1) { Write-Host "HSBUILD THUNK FAIL positive control absent for $cls; no verdict on its functions"; $ok = $false }
            if ($neg -ne 0) { Write-Host "HSBUILD THUNK FAIL absent-name control matched $neg time(s) for $cls"; $ok = $false }
        }
        foreach ($w in $want) {
            $parts = $w -split '::'
            if ($parts.Count -ne 2 -or $parts[0] -notmatch '^[A-Za-z_]\w*$' -or $parts[1] -notmatch '^[A-Za-z_]\w*$') { Write-Host "HSBUILD THUNK FAIL bad name '$w' (expected Class::Function)"; $ok = $false; continue }
            $n = Get-Count $text ('?' + 'ex' + 'ec' + $parts[1] + '@' + $parts[0] + '@@')
            Write-Host "HSBUILD THUNK function=$w classQualifiedThunkExports=$n"
            $checked++
            if ($n -lt 1) { Write-Host "HSBUILD THUNK FAIL MISSING $w"; $ok = $false }
        }
        }
    }
    Write-Host "HSBUILD THUNK SUMMARY functionsChecked=$checked pass=$ok"
    return $ok
}

function Invoke-PostGates($RunStart) {
    $logOk = $true
    if ($SkipUhtLogGate) { Write-Host "HSBUILD UHT-LOG skipped (-SkipUhtLogGate); header-tool evidence NOT checked" }
    else { $logOk = Test-UhtLog $RunStart }
    $thunkOk = Test-Thunks $RunStart
    Write-Host "HSBUILD GATES uhtLog=$(if ($SkipUhtLogGate) { 'skipped' } else { $logOk }) thunks=$thunkOk"
    if (-not $thunkOk) { return 33 }
    if (-not $logOk) { return 32 }
    return 0
}

if ($SkipUhtLogGate -and -not $VerifyOnly) { Write-Host "HSBUILD FAIL -SkipUhtLogGate is accepted only with -VerifyOnly"; exit 1 }
if ($VerifyOnly) {
    Write-Host "HSBUILD VERIFY-ONLY mod=$ModName targets=$($UhtTargets -join ',') (no build, no sync, no rename)"
    try { $vg = Invoke-PostGates $null } catch { Write-Host "HSBUILD GATES threw: $($_.Exception.Message)"; $vg = 33 }; exit $vg
}

$script = Join-Path $SkillScripts ($Package ? "package_release.ps1" : "build_mod.ps1")
if (-not (Test-Path $script)) { Write-Error "Skill script not found: $script"; exit 1 }

# B-F3 pin: the manifest's liveLatencySeconds is a hand copy of the subsystem's refresh interval.
if ($ModName -eq 'HostileScaling') {
    $subPath = Join-Path $SourceRoot "$ModName\Source\$ModName\Private\HostileScalingSubsystem.cpp"
    $mcpPath = Join-Path $SourceRoot "$ModName\Source\$ModName\Private\HostileScalingMcp.cpp"
    $sub = Get-Content -Raw -LiteralPath $subPath -ErrorAction SilentlyContinue
    $mcp = Get-Content -Raw -LiteralPath $mcpPath -ErrorAction SilentlyContinue
    if (-not $sub -or -not $mcp) { Write-Host "HSBUILD PIN FAIL could not read $subPath or $mcpPath"; exit 35 }
    $a = [regex]::Match($sub, 'RefreshIntervalSeconds\s*=\s*([0-9]+)'); $b = [regex]::Match($mcp, 'McpRefreshIntervalSeconds\s*=\s*([0-9]+)')
    Write-Host "HSBUILD PIN refresh=$($a.Groups[1].Value) mcpLatency=$($b.Groups[1].Value) files=$subPath,$mcpPath"
    if (-not $a.Success -or -not $b.Success -or $a.Groups[1].Value -ne $b.Groups[1].Value) { Write-Host "HSBUILD FAIL liveLatencySeconds copy drifted"; exit 35 }
}

$runStart = [DateTime]::UtcNow
try { $touched = Update-HeaderMtimes } catch { Write-Host "HSBUILD UHT-FORCE threw: $($_.Exception.Message)"; $touched = $false }
if (-not $touched) { Write-Host "HSBUILD FAIL header mtime update incomplete, or a UHT stamp is not older than the touch time (UHT-FORCE lines above); nothing delegated"; exit 34 }

$disabled = "$WuPlugin.disabled_for_build"
$renamed = $false
if (Test-Path $WuPlugin) {
    Rename-Item -Path $WuPlugin -NewName (Split-Path $disabled -Leaf)
    $renamed = $true
    Write-Host "WeaponUpgrades.uplugin -> .disabled_for_build"
} elseif (-not (Test-Path $disabled)) {
    Write-Host "WeaponUpgrades plugin not present - nothing to disable."
}

try {
    $global:LASTEXITCODE = $null
    try { & $script -ModName $ModName }
    catch { Write-Host "HSBUILD CHILD threw: $($_.Exception.Message)"; exit 31 }
    $childCode = $global:LASTEXITCODE
    if (-not ($childCode -is [int])) { Write-Host "HSBUILD CHILD exitCode=$(if ($null -eq $childCode) { 'null' } else { "'$childCode'" }) (not an integer) -> failure"; exit 30 }
    Write-Host "HSBUILD CHILD exitCode=$childCode"
    # 17 = built and deployed, AT post gate failed: the DLL is live, so run the gates for the record.
    if ($childCode -ne 0 -and $childCode -ne 17) { Write-Host "HSBUILD GATES not run (child exit $childCode)"; exit $childCode }
    try { $gate = Invoke-PostGates $runStart } catch { Write-Host "HSBUILD GATES threw: $($_.Exception.Message)"; $gate = 33 }
    if ($gate -ne 0 -or $childCode -ne 0) { Write-Host "HSBUILD DEPLOYED-UNVERIFIED: this run deployed a new DLL to $GameDir\FactoryGame\Mods\$ModName$(if ($Package) { " and wrote a release zip under $StarterProject\Saved\ArchivedPlugins\$ModName (do not upload it)" }) and a post-build gate failed (child $childCode, gates $gate). Do not launch the game on it until the last reviewed binary is restored or the gate is cleared." }
    # A gate failure (32/33) outranks the child's 17: it is the reflection verdict; 17 is printed above by the child.
    exit $(if ($gate -ne 0) { $gate } else { $childCode })
}
finally {
    if ($renamed -and (Test-Path $disabled)) {
        Rename-Item -Path $disabled -NewName (Split-Path $WuPlugin -Leaf)
        Write-Host "WeaponUpgrades.uplugin restored."
    }
}
