# Tests Balloon Tasks! as installed on Windows: the installer, the sign-in
# autostart, and the x64 build. Run after `make installer`:
#
#   powershell -ExecutionPolicy Bypass -File tools\test-windows.ps1 [-Only install,autostart,x64]
#
# It installs, uninstalls and launches the app for the current user, and puts
# back the preferences file it found. The app is left installed and stopped.
param(
    [string[]]$Only = @('install', 'autostart', 'x64')
)
$ErrorActionPreference = 'Stop'

# -File passes "install,x64" as one string.
$Only = @($Only -split ',' | Where-Object { $_ })
$unknown = $Only | Where-Object { $_ -notin 'install', 'autostart', 'x64' }
if ($unknown) {
    throw "Unknown test: $($unknown -join ', '). Use install, autostart or x64."
}

$repo = Split-Path -Parent $PSScriptRoot
$work = Join-Path $env:TEMP 'balloon-tasks-test'
$setup = Get-ChildItem "$repo\installer\balloon-tasks-*-setup.exe" -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime | Select-Object -Last 1
$iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
$prefs = "$env:APPDATA\balloon-tasks.preferences"
$dir = "$env:LOCALAPPDATA\Programs\balloon-tasks"
$app = "$dir\balloon-tasks.exe"
$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Balloon Tasks!.lnk'
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'Balloon Tasks!.lnk'
$native = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$script:failures = 0

function Check([string]$what, [bool]$ok) {
    if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:failures++ }
}

# arm64 or x64, from the PE header.
function Machine([string]$file) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    switch ([BitConverter]::ToUInt16($bytes, $pe + 4)) {
        0xAA64 { 'arm64' }
        0x8664 { 'x64' }
        default { 'other' }
    }
}

function Wait-Until([scriptblock]$condition, [int]$seconds = 15) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        if (& $condition) { return $true }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}

function App { Get-Process -Name balloon-tasks -ErrorAction SilentlyContinue }

function Stop-App {
    App | Stop-Process -Force -ErrorAction SilentlyContinue
    $null = Wait-Until { -not (App) } 5
}

# Waits for the setup program alone; the app its last page launches keeps running.
function Invoke-Setup([string]$exe) {
    $process = Start-Process -FilePath $exe -PassThru `
        -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CLOSEAPPLICATIONS'
    $process.WaitForExit()
    $process.ExitCode
}

function Get-Pref([string]$key) {
    (Get-Content $prefs | Where-Object { $_ -like "$key *" }) -replace "^$key ", ''
}

function Set-TestPrefs([int]$closed) {
    $text = "BALLOON_TASKS_PREFS 2`nwindow 0 0`nstarted 1`nclosed $closed`ncount 2`n" +
            "task 0|test one`ntask 1|test two!`n"
    [IO.File]::WriteAllText($prefs, $text)
}

function Get-UninstallEntry {
    Get-ChildItem HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall |
        Get-ItemProperty -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -eq 'Balloon Tasks!' }
}

function Test-Install {
    'install'
    Stop-App
    Check 'installer exits 0' ((Invoke-Setup $setup.FullName) -eq 0)
    Check 'installer launches the app' (Wait-Until { [bool](App) })
    Check "installed program is $native" ((Machine $app) -eq $native)
    Check 'Start menu shortcut' (Test-Path -LiteralPath $menu)
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($startup)
    Check 'sign-in shortcut runs --autostart' ((Test-Path -LiteralPath $startup) -and
        $link.TargetPath -eq $app -and $link.Arguments -eq '--autostart')
    Check 'listed in installed apps' ([bool](Get-UninstallEntry))

    $old = @(App).Id
    Check 'upgrade while running exits 0' ((Invoke-Setup $setup.FullName) -eq 0)
    Check 'upgrade replaces the running app' (Wait-Until {
        $now = @(App).Id
        $now.Count -gt 0 -and -not ($now | Where-Object { $_ -in $old })
    })
    Check 'upgrade is not a deliberate close' ((Get-Pref closed) -eq '0')

    Stop-App
    Check 'uninstaller exits 0' ((Invoke-Setup "$dir\unins000.exe") -eq 0)
    Check 'program removed' (Wait-Until { -not (Test-Path $dir) })
    Check 'shortcuts removed' (-not (Test-Path -LiteralPath $menu) -and -not (Test-Path -LiteralPath $startup))
    Check 'removed from installed apps' (-not (Get-UninstallEntry))
    Check 'preferences kept' (Test-Path $prefs)

    Check 'reinstall exits 0' ((Invoke-Setup $setup.FullName) -eq 0)
    $null = Wait-Until { [bool](App) }
    Stop-App
}

function Test-Autostart {
    'autostart'
    if (-not (Test-Path -LiteralPath $startup)) {
        Check 'installed with the sign-in shortcut' $false
        return
    }
    Stop-App
    Set-TestPrefs 1
    Start-Process -FilePath $startup
    Start-Sleep 6
    Check 'after a deliberate quit, a sign-in start exits' (-not (App))
    Check '  and keeps the flag' ((Get-Pref closed) -eq '1')

    Set-TestPrefs 0
    Start-Process -FilePath $startup
    Check 'otherwise a sign-in start runs' (Wait-Until { [bool](App) })
    Start-Sleep 3
    Check '  and loads the saved tasks' ((Get-Pref count) -eq '2')
    $null = (App | Select-Object -First 1).CloseMainWindow()
    Check 'taskbar close quits' (Wait-Until { -not (App) })
    Check '  and counts as deliberate' ((Get-Pref closed) -eq '1')

    Start-Process -FilePath $startup
    Start-Sleep 6
    Check 'the next sign-in start exits' (-not (App))

    Start-Process -FilePath $menu
    Check 'a Start menu start runs' (Wait-Until { [bool](App) })
    Start-Sleep 3
    Check '  and clears the flag' ((Get-Pref closed) -eq '0')
    Stop-App
}

# Installs the x64 build whatever this machine is, then the matching one again.
function Test-X64 {
    'x64'
    if (-not (Test-Path $iscc)) {
        Check 'Inno Setup 6 installed' $false
        return
    }
    $version = $setup.BaseName -replace '^balloon-tasks-(.*)-setup$', '$1'
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    & $iscc /Q "/DAppVersion=$version" /DForceArch=x64 "/O$work" /Fballoon-tasks-x64-test "$repo\balloon-tasks.iss"
    Check 'x64 test installer builds' ($LASTEXITCODE -eq 0)

    Stop-App
    Check 'x64 install exits 0' ((Invoke-Setup "$work\balloon-tasks-x64-test.exe") -eq 0)
    $files = @(Get-ChildItem "$dir\*" -Include *.exe, *.dll | Where-Object { $_.Name -notlike 'unins*' })
    Check 'every installed program file is x64' ($files.Count -ge 2 -and
        -not ($files | Where-Object { (Machine $_.FullName) -ne 'x64' }))
    Check 'x64 build runs' (Wait-Until { [bool](App) })
    Start-Sleep 10
    Check '  and keeps running' ([bool](App))

    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bitmap.Save("$work\x64.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    "  screenshot: $work\x64.png"

    Stop-App
    Check 'reinstall exits 0' ((Invoke-Setup $setup.FullName) -eq 0)
    $null = Wait-Until { [bool](App) }
    Stop-App
    Check "installed program is $native again" ((Machine $app) -eq $native)
}

if (-not $setup) {
    throw "No installer in $repo\installer; run make installer first."
}
"installer: $($setup.Name)"
$saved = if (Test-Path $prefs) { [IO.File]::ReadAllText($prefs) } else { $null }
try {
    if ('install' -in $Only) { Test-Install }
    if ('autostart' -in $Only) { Test-Autostart }
    if ('x64' -in $Only) { Test-X64 }
} finally {
    Stop-App
    if ($null -ne $saved) {
        [IO.File]::WriteAllText($prefs, $saved)
    } else {
        Remove-Item $prefs -ErrorAction SilentlyContinue
    }
}
if ($script:failures) {
    "$($script:failures) failed"
    exit 1
}
'all passed'
