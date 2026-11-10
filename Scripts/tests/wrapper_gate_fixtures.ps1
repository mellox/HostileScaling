# Fixture tests for Scripts\build_wu_disabled.ps1 post-build gates (packet mcp3).
# Self-contained: everything is created under a fresh $env:TEMP subfolder and the wrapper is pointed at
# it through its existing path parameters (-SourceRoot -StarterProject -GameDir -SkillScripts -WuPlugin).
# It never touches the real WeaponUpgrades plugin, the real mirror, the deployed DLL or the build lock,
# and never starts a real build (the "child" is a fake build_mod.ps1 written into the fixture tree).
# The fake DLL is a byte file carrying the export-name strings the thunk gate scans for.
# Usage: pwsh -NoProfile -File Scripts\tests\wrapper_gate_fixtures.ps1 [-Mode <name>]
# Exit 0 only when every mode run observed its expected exit code and marker lines.
param([string]$Mode = 'all')
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$wrapper = Join-Path $repo 'Scripts\build_wu_disabled.ps1'
$tempBase = [IO.Path]::GetFullPath($env:TEMP)
$root = Join-Path $tempBase ('hs-wrapper-fixtures-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$mod = 'HostileScaling'
# Exported UFUNCTIONs the thunk gate derives today (AHostileScalingSubsystem::McpDescribe/McpSnapshot).
# If the headers gain one, the fresh mode fails 33 and this list must grow.
$cls = 'AHostileScalingSubsystem'; $fns = @('McpDescribe', 'McpSnapshot')

function New-Fixture([string]$Name, [string]$ObjMode, [switch]$NoRsp) {
    $S = Join-Path $root $Name
    $p = [ordered]@{ SourceRoot = "$S\src"; StarterProject = "$S\smlp"; GameDir = "$S\game"; SkillScripts = "$S\skill"; WuPlugin = "$S\wu\WeaponUpgrades.uplugin" }
    foreach ($v in $p.Values) { if (-not [IO.Path]::GetFullPath($v).StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "refusing: $v is outside $root" } }
    $null = robocopy (Join-Path $repo 'Source') "$S\src\$mod\Source" /E /NFL /NDL /NJH /NJS /NP
    $null = robocopy (Join-Path $repo 'Source') "$S\smlp\Mods\$mod\Source" /E /NFL /NDL /NJH /NJS /NP
    $objDir = "$S\smlp\Mods\$mod\Intermediate\Build\Win64\x64\FactoryGameSteam\Shipping\$mod"
    $binDir = "$S\game\FactoryGame\Mods\$mod\Binaries\Win64"
    New-Item -ItemType Directory -Force $objDir, $binDir, "$S\skill", "$S\wu", "$S\tmp" | Out-Null
    $obj = "$objDir\Module.$mod.cpp.obj"
    [IO.File]::WriteAllText($obj, 'fixture obj')
    [IO.File]::SetLastWriteTimeUtc($obj, [DateTime]'2020-01-01')
    if (-not $NoRsp) {
        $lines = @('/DLL', '"C:/Fixture/Engine/SharedPCH.obj"')
        if ($ObjMode -ne 'none') { $lines += ('"' + $obj.Replace('\', '/') + '"') }
        $lines += ('"' + "$objDir\Default.rc2.res".Replace('\', '/') + '"')
        $lines += ('/OUT:"' + "$S\smlp\Mods\$mod\Binaries\Win64\FactoryGameSteam-$mod-Win64-Shipping.dll".Replace('\', '/') + '"')
        Set-Content -LiteralPath "$objDir\FactoryGameSteam-$mod-Win64-Shipping.dll.rsp" -Value $lines
    }
    $dll = "$binDir\FactoryGameSteam-$mod-Win64-Shipping.dll"
    $names = @('?' + 'StaticClass@' + $cls + '@@') + @($fns | ForEach-Object { '?' + 'exec' + $_ + '@' + $cls + '@@' })
    [IO.File]::WriteAllBytes($dll, [Text.Encoding]::ASCII.GetBytes('MZ fixture ' + ($names -join ' ')))
    [IO.File]::SetLastWriteTimeUtc($dll, [DateTime]'2020-01-01')
    [IO.File]::WriteAllText("$S\wu\WeaponUpgrades.uplugin", '{}')
    [IO.File]::SetLastWriteTimeUtc("$S\wu\WeaponUpgrades.uplugin", [DateTime]'2021-02-03')
    # Fake child: header-tool log, "deploy" (touch the DLL), recompile (touch the object) unless stale.
    @"
param([string]`$ModName)
Set-Content -LiteralPath (Join-Path `$env:TEMP "alpakit_`$ModName.log") -Value @('  Parsing headers for FactoryGameSteam', '  Reflection code generated for FactoryGameSteam in 1 seconds')
[IO.File]::SetLastWriteTimeUtc('$dll', [DateTime]::UtcNow)
if ('$ObjMode' -eq 'fresh') { [IO.File]::SetLastWriteTimeUtc('$obj', [DateTime]::UtcNow) }
Write-Host "FAKE CHILD wuPresent=`$(Test-Path '$S\wu\WeaponUpgrades.uplugin') disabledPresent=`$(Test-Path '$S\wu\WeaponUpgrades.uplugin.disabled_for_build')"
`$global:LASTEXITCODE = 0
"@ | Set-Content -LiteralPath "$S\skill\build_mod.ps1"
    return [pscustomobject]@{ Dir = $S; Params = $p }
}

function Invoke-Case([string]$Name, [string]$ObjMode, [int]$Expect, [string[]]$MustContain, [string[]]$Extra = @(), [switch]$NoRsp, [switch]$Keep) {
    $fx = New-Fixture $Name $ObjMode -NoRsp:$NoRsp
    $wu = $fx.Params.WuPlugin
    $args2 = @($fx.Params.GetEnumerator() | ForEach-Object { "-$($_.Key)"; $_.Value }) + $Extra
    if ($Keep) { $args2 += '-KeepWeaponUpgrades' }
    $oldTemp = $env:TEMP
    try { $env:TEMP = "$($fx.Dir)\tmp"; $out = @(& pwsh -NoProfile -File $wrapper @args2 2>&1 | ForEach-Object { "$_" }); $code = $LASTEXITCODE }
    finally { $env:TEMP = $oldTemp }
    Set-Content -LiteralPath "$($fx.Dir)\wrapper-output.txt" -Value $out
    $why = @()
    if ($code -ne $Expect) { $why += "exit $code, expected $Expect" }
    foreach ($m in $MustContain) { if (-not ($out | Where-Object { $_.StartsWith($m) -or $_.Contains(' ' + $m) })) { $why += "missing '$m'" } }
    if (-not (Test-Path -LiteralPath $wu)) { $why += 'scratch WeaponUpgrades.uplugin absent after the run' }
    if (Test-Path -LiteralPath "$wu.disabled_for_build") { $why += '.disabled_for_build left behind' }
    if ($Keep -and ($out | Where-Object { $_ -like 'WeaponUpgrades.uplugin -> *' })) { $why += 'renamed despite -KeepWeaponUpgrades' }
    $verdict = if ($why.Count) { 'FAIL' } else { 'PASS' }
    Write-Host ("FIXTURE {0} {1} exit={2} expected={3}{4} output={5}\wrapper-output.txt" -f $verdict, $Name, $code, $Expect, $(if ($why.Count) { ' (' + ($why -join '; ') + ')' } else { '' }), $fx.Dir)
    return ($why.Count -eq 0)
}

$cases = [ordered]@{
    'fresh'           = { Invoke-Case 'fresh' 'fresh' 0 @('HSBUILD GATES uhtLog=True thunks=True objects=True', 'WeaponUpgrades.uplugin restored.') }
    'stale'           = { Invoke-Case 'stale' 'stale' 37 @('HSBUILD OBJ STALE', 'HSBUILD DEPLOYED-UNVERIFIED', 'WeaponUpgrades.uplugin restored.') }
    'stale-keep'      = { Invoke-Case 'stale-keep' 'stale' 37 @('HSBUILD OBJ STALE', 'HSBUILD WU rename=skipped') -Keep }
    'none'            = { Invoke-Case 'none' 'none' 37 @('zero objects under', 'HSBUILD DEPLOYED-UNVERIFIED') }
    'thunk-missing'   = { Invoke-Case 'thunk-missing' 'fresh' 33 @('HSBUILD THUNK FAIL MISSING') @('-ExtraThunk', "${cls}::NoSuchFn") }
    'verify-no-rsp'   = { Invoke-Case 'verify-no-rsp' 'fresh' 37 @('HSBUILD OBJ FAIL target=FactoryGameSteam module=HostileScaling rsp=', 'HSBUILD GATES uhtLog=skipped thunks=True objects=False objectFreshness=not-tested') @('-VerifyOnly', '-SkipUhtLogGate') -NoRsp }
    'verify-ok'       = { Invoke-Case 'verify-ok' 'fresh' 0 @('HSBUILD OBJ SUMMARY targets=FactoryGameSteam objectsListed=1 stale=0 freshness=not-tested pass=True', 'HSBUILD GATES uhtLog=skipped thunks=True objects=True objectFreshness=not-tested') @('-VerifyOnly', '-SkipUhtLogGate') }
}
$run = if ($Mode -eq 'all') { @($cases.Keys) } elseif ($cases.Contains($Mode)) { @($Mode) } else { Write-Host "unknown mode $Mode; modes: $($cases.Keys -join ', ')"; exit 2 }
New-Item -ItemType Directory -Force $root | Out-Null
$pass = 0
foreach ($k in $run) { if (& $cases[$k]) { $pass++ } }
Write-Host ("FIXTURE SUMMARY pass={0} fail={1} of {2} root={3}" -f $pass, ($run.Count - $pass), $run.Count, $root)
exit $(if ($pass -eq $run.Count) { 0 } else { 1 })
