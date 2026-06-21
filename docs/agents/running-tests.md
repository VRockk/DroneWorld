# Running automation tests

DroneWorld's C++ tests are Unreal **automation tests** (`IMPLEMENT_SIMPLE_AUTOMATION_TEST` /
`FAutomationTestBase`), living as `.cpp` files under `Source/DroneWorld/Private/Tests/`. They compile
into the `DroneWorld` module behind `#if WITH_AUTOMATION_TESTS`, so no separate module or
`.Build.cs` change is needed — UBT picks up any `.cpp` under the module's `Source` tree.

There is no inline build/test trigger inside the engine, but an agent **can** build and run the
tests from the command line and read a structured report. Two steps: build, then run.

## 1. Build (UBT)

Build the editor target so the test code compiles:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" `
    DroneWorldEditor Win64 Development `
    -Project="C:\Users\Soyhan\Desktop\DroneWorld\DroneWorld.uproject" -WaitMutex
```

- Target: `DroneWorldEditor` · Platform: `Win64` · Configuration: `Development`.
- A clean build is minutes; incremental builds after a small edit are much faster.
- Builds are expensive, so verify engine API spellings against the 5.8 headers
  (`C:\Program Files\Epic Games\UE_5.8\Engine\Source`) before building rather than guessing.

## 2. Run (UnrealEditor-Cmd.exe)

Run by test-name prefix and emit a structured JSON report instead of scraping the log:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "C:\Users\Soyhan\Desktop\DroneWorld\DroneWorld.uproject" `
    -ExecCmds="Automation RunTests DroneWorld.Flight" `
    -TestExit="Automation Test Queue Empty" `
    -ReportOutputPath="C:\Users\Soyhan\Desktop\DroneWorld\.scratch\test-report" `
    -unattended -nullrhi -nosplash -nopause -log
```

- `Automation RunTests <prefix>` matches tests by name prefix (e.g. `DroneWorld.Flight`).
- `-TestExit="Automation Test Queue Empty"` quits the editor once the run finishes (don't rely on
  appending `; Quit`, which can fire before the async run completes).
- `-nullrhi` is safe for pure-logic tests (no GPU). Drop it if a test needs the RHI.
- The report lands at `<ReportOutputPath>\index.json` — read that for pass/fail per test rather
  than the full log. (If no report appears, the arg is `-ReportExportPath` on some builds.)

## Reading the report

Summarize `index.json` instead of scrolling the log — overall counts, per-test state, and the
failure messages for anything that didn't pass:

```powershell
$json = Get-Content "<ReportOutputPath>\index.json" -Raw | ConvertFrom-Json
Write-Output ("succeeded={0} failed={1} notRun={2}" -f $json.succeeded, $json.failed, $json.notRun)
$json.tests | ForEach-Object { Write-Output ("  [{0}] {1}" -f $_.state, $_.fullTestPath) }
$json.tests | Where-Object { $_.state -ne "Success" } |
    ForEach-Object { $_.entries | ForEach-Object { Write-Output ("    ! " + $_.event.message) } }
```

## Conventions

- **Test names**: `DroneWorld.<Area>.<Subject>.<Behavior>`, e.g.
  `DroneWorld.Flight.Quad.HoldsAltitudeInHover`. The prefix is what you filter on.
- **Flags** (5.8, `EAutomationTestFlags` is an `enum class`):
  `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`.
- Tests target C++ interfaces only — Blueprint `.uasset` files are binary and out of reach.
- Never touch `UPROPERTY` / `UFUNCTION` macros for testability — don't reshape reflected
  members or expose engine-facing surface just to make something easier to test.
- Prefer testing through the highest pure seam (e.g. a flight model's force computation —
  state + control intent in, force/torque out) so tests need no live `UWorld`, actors, or pawn.
```
