# Packages the web-standalone build into a private, self-contained handoff zip.
#
# Unlike package_release.ps1 (the public Windows release, BYO original-game
# files), the web build's game data is baked in at build time (see README
# "Browser build") -- so this zip is playable out of the box, and MUST NOT be
# published as a GitHub release or committed: it redistributes Loriciel's
# copyrighted 1993 game data. It's for a one-off private handoff only.
#
# Uses the web-standalone preset (BUMPY_WEB_STANDALONE=ON), not web-release:
# that embeds the wasm and game data as base64 straight into openbumpy.html
# instead of separate .wasm/.data files fetched via XHR, so it opens directly
# from a double-clicked file:// URL with no local webserver needed -- XHR to
# a local file is what browsers block under file://, a <script> tag isn't.

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = "v0.1.0"
$stageName = "OpenBumpy-web-$version-PRIVATE"

Push-Location $root
try {
    $buildDir = "build/web-standalone"
    $files = "openbumpy.html"

    foreach ($f in $files) {
        if (-not (Test-Path (Join-Path $buildDir $f))) {
            throw "$f not found in $buildDir -- run: cmake --build --preset web-standalone"
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
