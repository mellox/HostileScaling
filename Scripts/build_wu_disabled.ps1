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
#
# Delegates the actual work (game-check, cross-session lock, mirror sync, UAT,
# verification) to the satisfactory-modding skill scripts.

param(
    [string]$ModName = "HostileScaling",
    [switch]$Package,
    [string]$SkillScripts = "$env:USERPROFILE\.claude\skills\satisfactory-modding\scripts",
    [string]$WuPlugin = "C:\Claude\Projects\SatisfactoryModLoader\Mods\WeaponUpgrades\WeaponUpgrades.uplugin"
)

$ErrorActionPreference = "Stop"

if ($ModName -eq "WeaponUpgrades") {
    Write-Error "WeaponUpgrades builds itself normally - call the skill script directly."
    exit 1
}

$script = Join-Path $SkillScripts ($Package ? "package_release.ps1" : "build_mod.ps1")
if (-not (Test-Path $script)) { Write-Error "Skill script not found: $script"; exit 1 }

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
    & $script -ModName $ModName
    exit $LASTEXITCODE
}
finally {
    if ($renamed -and (Test-Path $disabled)) {
        Rename-Item -Path $disabled -NewName (Split-Path $WuPlugin -Leaf)
        Write-Host "WeaponUpgrades.uplugin restored."
    }
}
