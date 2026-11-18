# Builds the release version of Cronos and installs it for the current user:
#   - the app, with the Qt files it needs, in %LOCALAPPDATA%\Programs\Cronos
#   - a Start menu shortcut
#   - started by Windows at sign-in, hidden in the tray (switch it off in the app)
# Run again after changing the code to update the installed copy.
#
#   powershell -ExecutionPolicy Bypass -File install.ps1
#
# To remove it: quit Cronos from the tray, switch off "Start when I sign in"
# first, then delete the folder and the shortcut.
$qt = 'C:\Qt\6.11.2\mingw_64'   # where Qt is on this machine
$tools = 'C:\Qt\Tools'
$ErrorActionPreference = 'Stop'

$env:PATH = "$qt\bin;$tools\mingw1310_64\bin;$tools\CMake_64\bin;$tools\Ninja;$env:PATH"
$build = "$PSScriptRoot\build\Release"
cmake -S $PSScriptRoot -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
if ($LASTEXITCODE) { throw 'Configuring failed' }
cmake --build $build --target cronos
if ($LASTEXITCODE) { throw 'Building failed' }

# A running copy keeps its files locked.
Get-Process cronos -ErrorAction SilentlyContinue | Stop-Process
Start-Sleep -Seconds 1

$app = "$env:LOCALAPPDATA\Programs\Cronos"
$firstInstall = -not (Test-Path $app)
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item "$build\cronos.exe" $app -Force
windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --compiler-runtime "$app\cronos.exe" | Out-Null
if ($LASTEXITCODE) { throw 'Copying the Qt files failed' }

$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut("$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Cronos.lnk")
$shortcut.TargetPath = "$app\cronos.exe"
$shortcut.WorkingDirectory = $app
$shortcut.Description = 'Run actions by tapping the laptop'
$shortcut.Save()

# The same entry the app's "Start when I sign in to Windows" switch writes. Only
# on the first install: an update must not switch it back on after you turned it off.
if ($firstInstall) {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'Cronos' -Value "`"$app\cronos.exe`" --hidden"
}

Start-Process "$app\cronos.exe"
"Installed in $app and started."
