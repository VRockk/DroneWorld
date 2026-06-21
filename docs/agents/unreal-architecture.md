# Analyzing the architecture of this Unreal project

Guidance for `/improve-codebase-architecture` (and any architecture analysis) on DroneWorld.
Unreal Engine projects have structural rules that don't show up in plain C++ codebases — what
looks like a smell is often a required engine pattern, and the most important seam in the project
is invisible to text tooling. Read this before generating candidates.

## Project-specific patterns that are NOT smells

These are deliberate UE5 patterns. Do **not** flag them as shallow modules or propose
"deepening" them away:

- **`UGameInstance` singleton** — the one object that survives level transitions; the natural
  home for app-lifetime state. Singleton-ness is the point, not an accident.
- **`UPROPERTY(EditDefaultsOnly)` spawn classes** — `TSubclassOf<>` fields wired in the editor
  so designers pick the spawned class without a code change. The indirection is intentional.
- **`GameMode` ↔ `GameInstance` startup wiring** — the standard handshake for bringing systems
  up in order. It reads as ceremony but it's the engine's lifecycle contract.
- **Delegate-based motion updates** — `BlueprintAssignable` / multicast delegates broadcasting
  motion changes. The fan-out is the design; collapsing it would couple producers to consumers.

## The C++/Blueprint boundary is the load-bearing seam — and you can't see it

`.uasset` / `.umap` files are binary. You cannot read a Blueprint graph, a material, or a level.
That has direct consequences for analysis:

- **Reason about Blueprints from their C++ base classes.** A `UCLASS`'s `UPROPERTY` /
  `UFUNCTION` surface *is* the contract Blueprints are written against. To understand what a
  Blueprint can do, read the C++ it derives from.
- **Reflection macros are interface, not implementation.** `UCLASS` / `USTRUCT` /
  `UPROPERTY` / `UFUNCTION` / `BlueprintCallable` are a published interface consumed by
  Blueprints, serialization, networking, and the editor. Renaming or removing a reflected
  member silently breaks `.uasset` references that no compiler will catch. Treat the reflected
  surface as frozen unless a change is explicitly in scope.
- **Flag any candidate that crosses the C++/Blueprint boundary** — it needs manual editor work,
  so it can't be an AFK slice. Call this out in the candidate.

## How to find real depth vs. shallowness in UE

- **Deletion test, UE flavor.** A `UObject` that just forwards calls to another system is a
  pass-through — deleting it concentrates complexity usefully. A `UGameInstanceSubsystem` /
  `UWorldSubsystem` that owns a lifecycle and a chunk of behavior behind a small interface is a
  deep module — leave it, or look for more like it.
- **Subsystems are the deepening tool.** When several actors reach into the same shared state,
  a subsystem is usually the right deep seam — engine-managed lifetime, single owner, small
  interface. Prefer it over a hand-rolled singleton or a static helper.
- **The `Public/` vs `Private/` split is the primary module seam.** Anything in `Public/` is
  interface for other modules; `Private/` is implementation. A header in `Public/` that nothing
  outside the module includes is a leak worth noting.
- **`.Build.cs` dependency lists are where coupling actually lives.**
  `PublicDependencyModuleNames` vs `PrivateDependencyModuleNames` tells you the real module
  graph — read it before claiming two modules are or aren't coupled.
- **Pure seams are the testable ones.** A function that takes engine types (`UWorld*`,
  actors, subsystems) can only be tested in a live editor. A function that takes plain data and
  returns plain data (e.g. a parse that returns a snapshot struct) is testable in isolation —
  these are the highest-value deepening targets. See `docs/agents/running-tests.md`.

## Process constraints

- **Read `docs/adr/` before generating candidates.** ADRs record decisions the analysis must
  not re-litigate; respect them, and only reopen one when the friction is real enough to warrant
  it (mark it clearly when you do).
- **This project targets `Win64` only.** Don't add other-platform handling or cross-platform
  candidates — Win64 is the sole shipping platform.
