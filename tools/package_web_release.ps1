# Packages the web build into a private, self-contained handoff zip.
#
# Unlike package_release.ps1 (the public Windows release, BYO original-game
# files), the web build's openbumpy.data has the original 47 resource files
# baked in at build time (see README "Browser build") -- so this zip is
# playable out of the box, and MUST NOT be published as a GitHub release or
# committed: it redistributes Loriciel's copyrighted 1993 game data. It's for
# a one-off private handoff only. See dist/README-PRIVATE.txt in the zip.

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = "v0.1.0"
$stageName = "OpenBumpy-web-$version-PRIVATE"

Push-Location $root
try {
    $buildDir = "build/web-release"
    $files = "openbumpy.html", "openbumpy.js", "openbumpy.wasm", "openbumpy.data"

    foreach ($f in $files) {
        if (-not (Test-Path (Join-Path $buildDir $f))) {
            throw "$f not found in $buildDir -- run: cmake --build --preset web-release"
        }
    }

    $stageRoot = "dist-private"
    $stage = "$stageRoot/$stageName"
    if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot -Confirm:$false }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    foreach ($f in $files) {
        Copy-Item (Join-Path $buildDir $f) $stage
    }
    Copy-Item "tools/reference/PLAY_WEB.txt" (Join-Path $stage "PLAY.txt")

    $zip = "$stageRoot/$stageName.zip"
    Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force

    Write-Output "Packaged (PRIVATE, do not publish -- bundles the original game's assets): $zip"
} finally {
    Pop-Location
}
