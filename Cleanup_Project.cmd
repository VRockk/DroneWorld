@echo off
setlocal

set "PROJECT_PATH=%CD%"

echo Cleaning project folder: "%PROJECT_PATH%"

where git >nul 2>&1
if errorlevel 1 (
    echo Git was not found in PATH.
    pause >nul
    exit /b 1
)

git -C "%PROJECT_PATH%" rev-parse --show-toplevel >nul 2>&1
if errorlevel 1 (
    echo Current folder is not inside a Git repository.
    pause >nul
    exit /b 1
)

REM Send ignored files/folders to the Recycle Bin instead of deleting them permanently,
REM then recycle any folders left without files anywhere beneath them.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Add-Type -AssemblyName Microsoft.VisualBasic;" ^
  "$root = (git rev-parse --show-toplevel).Trim();" ^
  "$items = git clean -ndX | ForEach-Object { $_ -replace '^Would remove ', '' } | Where-Object { $_ -ne '' };" ^
  "foreach ($item in $items) {" ^
  "  $full = Join-Path $root $item;" ^
  "  if (Test-Path -LiteralPath $full -PathType Container) {" ^
  "    Write-Host \"Recycling folder: $full\";" ^
  "    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($full, 'OnlyErrorDialogs', 'SendToRecycleBin')" ^
  "  } elseif (Test-Path -LiteralPath $full) {" ^
  "    Write-Host \"Recycling file: $full\";" ^
  "    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($full, 'OnlyErrorDialogs', 'SendToRecycleBin')" ^
  "  }" ^
  "}" ^
  "Get-ChildItem -LiteralPath $root -Directory -Recurse -Force |" ^
  "  Where-Object { $_.FullName -notmatch '\\\.git(\\|$)' } |" ^
  "  Sort-Object { $_.FullName.Length } -Descending |" ^
  "  ForEach-Object {" ^
  "    if (Test-Path -LiteralPath $_.FullName) {" ^
  "      $files = Get-ChildItem -LiteralPath $_.FullName -File -Recurse -Force;" ^
  "      if (-not $files) {" ^
  "        Write-Host \"Recycling empty folder: $($_.FullName)\";" ^
  "        [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($_.FullName, 'OnlyErrorDialogs', 'SendToRecycleBin')" ^
  "      }" ^
  "    }" ^
  "  }"
if errorlevel 1 (
    echo Cleanup failed.
    pause >nul
    exit /b 1
)

echo Cleanup complete (items sent to Recycle Bin)
pause >nul
exit /b
