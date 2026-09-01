param(
    [string]$OutputDirectory = "$PSScriptRoot/distribution"
)

$ErrorActionPreference = "Stop"

$packageName = "com.theroughy.handoflesser.modular-avatar"
$packageSource = Join-Path $PSScriptRoot "UnityVPM"
$unitySource = Join-Path $PSScriptRoot "Unity/Assets/HandOfLesser"
$generatedSource = Join-Path $unitySource "generated"
$ownershipEditorSource = Join-Path $unitySource "FingerOwnershipEditor"
$stagingRoot = Join-Path $PSScriptRoot "build_cache/unity-vpm"
$packageRoot = Join-Path $stagingRoot $packageName
$runtimeRoot = Join-Path $packageRoot "Runtime"
$editorRoot = Join-Path $packageRoot "Editor"

$requiredAssets = @(
    (Join-Path $generatedSource "HandOfLesser_ModularAvatar.prefab"),
    (Join-Path $generatedSource "handoflesser_controller.controller"),
    (Join-Path $generatedSource "handoflesser_finger_tracking.controller"),
    (Join-Path $generatedSource "handoflesser_animations.asset"),
    (Join-Path $unitySource "vrc_handsonly.mask")
)

foreach ($asset in $requiredAssets) {
    if (!(Test-Path $asset) -or !(Test-Path "$asset.meta")) {
        throw "Missing generated package asset or metadata: $asset. Run the Unity Modular Avatar package builder first."
    }
}

if (Test-Path $stagingRoot) {
    Remove-Item $stagingRoot -Recurse -Force
}

New-Item $runtimeRoot -ItemType Directory -Force | Out-Null
New-Item $editorRoot -ItemType Directory -Force | Out-Null
Copy-Item "$packageSource/package.json" $packageRoot -Force
Copy-Item "$packageSource/README.md" $packageRoot -Force
Copy-Item "$generatedSource/*" $runtimeRoot -Recurse -Force
Copy-Item "$ownershipEditorSource/*" $editorRoot -Recurse -Force
Copy-Item "$unitySource/vrc_handsonly.mask" $runtimeRoot -Force
Copy-Item "$unitySource/vrc_handsonly.mask.meta" $runtimeRoot -Force
Copy-Item "$PSScriptRoot/LICENSE.md" "$packageRoot/LICENSE.md" -Force

$package = Get-Content "$packageRoot/package.json" -Raw | ConvertFrom-Json
$zipName = "$packageName-$($package.version).zip"
$zipPath = Join-Path $OutputDirectory $zipName

New-Item $OutputDirectory -ItemType Directory -Force | Out-Null
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive "$packageRoot/*" $zipPath -CompressionLevel Optimal
Write-Host "Created $zipPath"
