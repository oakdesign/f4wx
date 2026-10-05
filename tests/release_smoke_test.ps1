param(
    [string]$ExecutablePath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'bin/Release/F4Wx.exe'),
    [string]$ExpectedVersion = '2.3.0-beta.1'
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class F4WxBetaSmoke {
    public delegate bool WindowCallback(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr Find(uint processId, string prefix) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, data) => {
            uint process; GetWindowThreadProcessId(hwnd, out process);
            var title = new StringBuilder(512); GetWindowText(hwnd, title, title.Capacity);
            if (process == processId && title.ToString().StartsWith(prefix)) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$smokeExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$smokeApp = Start-Process -FilePath $smokeExecutable -WorkingDirectory (Split-Path $smokeExecutable) -WindowStyle Hidden -PassThru
try {
    $smokeApp.WaitForInputIdle(10000) | Out-Null
    $mainHandle = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $mainHandle = [F4WxBetaSmoke]::Find($smokeApp.Id, ('F4Wx v' + $ExpectedVersion))
        if ($mainHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($mainHandle -eq [IntPtr]::Zero) { throw 'Tagged beta application did not start.' }
    [F4WxBetaSmoke]::PostMessage($mainHandle, 0x111, [IntPtr]1601, [IntPtr]::Zero) | Out-Null
    $editorHandle = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $editorHandle = [F4WxBetaSmoke]::Find($smokeApp.Id, 'Weather Editor -')
        if ($editorHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($editorHandle -eq [IntPtr]::Zero) { throw 'New Weather did not open the editor in the release build.' }
    [F4WxBetaSmoke]::PostMessage($editorHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        if ([F4WxBetaSmoke]::Find($smokeApp.Id, 'Weather Editor -') -eq [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    [F4WxBetaSmoke]::PostMessage($mainHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $smokeExited = $smokeApp.WaitForExit(30000); Write-Output ("Exit: {0}; Code: {1}" -f $smokeExited, $smokeApp.ExitCode); if (!$smokeExited -or $smokeApp.ExitCode -ne 0) { throw 'Beta application did not shut down cleanly.' }
    Write-Output 'Release smoke test passed: tagged version, New Weather editor, and clean shutdown.'
} finally {
    $smokeApp.Refresh()
    if (!$smokeApp.HasExited) { Stop-Process -Id $smokeApp.Id }
}
