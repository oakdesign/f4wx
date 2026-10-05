param([string]$VisualStudioPath)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
if (!$VisualStudioPath) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $VisualStudioPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (!$VisualStudioPath) { throw 'A complete Visual Studio C++ installation is required.' }
$vcvars = Join-Path $VisualStudioPath 'VC/Auxiliary/Build/vcvars64.bat'
$compilerEnvironment = & cmd.exe /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE) { throw 'Cannot initialize the Visual Studio compiler environment.' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$output = Join-Path $taskRoot 'obj/editor-tests'
New-Item -ItemType Directory -Force $output | Out-Null
Push-Location $taskRoot
try {
    foreach ($variant in @('debug', 'release')) {
        $defines = @(if ($variant -eq 'debug') { '/D_DEBUG'; '/MTd' } else { '/DNDEBUG'; '/MT' })
        $executable = Join-Path $output "weather-tests-$variant.exe"
        & cl.exe /nologo /std:c++20 /EHsc /W3 /DNOMINMAX @defines /Isrc/f4wx "/Fo$output/" "/Fe$executable" tests/weather_editor_tests.cpp src/f4wx/fmap.cpp src/f4wx/weather_document.cpp /link user32.lib
        if ($LASTEXITCODE) { throw "Compilation failed ($variant)." }
        & $executable
        if ($LASTEXITCODE) { throw "Weather editor tests failed ($variant)." }
    }
    $resource = Join-Path $output 'editor.res'
    Push-Location src/f4wx
    try {
        & rc.exe /nologo "/fo$resource" resource.rc
        if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    } finally { Pop-Location }
    $uiExecutable = Join-Path $output 'weather-ui-tests.exe'
    & cl.exe /nologo /std:c++20 /EHsc /W3 /DNOMINMAX /D_DEBUG /MTd /Isrc/f4wx "/Fo$output/" "/Fe$uiExecutable" tests/weather_editor_ui_tests.cpp src/f4wx/fmap.cpp src/f4wx/weather_document.cpp src/f4wx/f4wx_preview.cpp $resource /link user32.lib gdi32.lib gdiplus.lib comctl32.lib comdlg32.lib
    if ($LASTEXITCODE) { throw 'Win32 editor compilation failed.' }
    & $uiExecutable
    if ($LASTEXITCODE) { throw 'Win32 editor tests failed.' }
} finally { Pop-Location }
