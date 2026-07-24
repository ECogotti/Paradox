# Debugging, profiling, and tests

## Project settings

The Entity Relations developer settings provide:

- cache enabled by default, maximum 1024 entries;
- global debug disabled by default;
- verbose explanation strings disabled by default;
- debug drawing disabled by default;
- a short configurable draw duration.

`EntityRelations.Debug` is a tri-state console variable: `-1` uses project settings, `0` disables global debug immediately, and `1` enables it. Debug drawing also requires the Source identity component's local `bEnableDebug`, project debug drawing, and an explicit diagnostic query. Effective drawing is therefore global AND local; ordinary cached queries never draw.

Diagnostic drawing uses a short-lived Source-to-Target arrow and a compact domain/decision label.

## Console commands

Run these in a world context:

```text
EntityRelations.List
EntityRelations.Dump <EntityId>
EntityRelations.Explain <SourceId> <TargetId> <DomainTag>
EntityRelations.ClearCache
EntityRelations.CacheStats
```

`Explain` performs an uncached diagnostic query and logs ordered policy trace, status, decision, winner, metadata, and revisions. IDs use the hyphenated GUID text emitted by `FEntityRelationId::ToString()`.

The module uses only `LogEntityRelations` and scoped Info/Warning/Error macros. A missing Policy Set warning is emitted once for a missing-configuration transition rather than once per query.

## Runtime statistics and Insights

`GetRuntimeStats()` reports query count, batch count, cache hits/misses, policies evaluated, live registry size, directed-state entries, and cache entries.

Unreal Insights exposes:

- `EntityRelations_EvaluateRelation`
- `EntityRelations_EvaluateBatch`
- `EntityRelations_ResolvePolicies`
- `EntityRelations_CacheLookup`

No profiling strings are assembled dynamically in the hot path.

## Automation Tests

The `EntityRelations.` suite uses transient Game worlds with RAII teardown and covers registry duplicate/replacement behavior, identity revisions, sparse directional state, change-only notifications, resolver priority/tie/stop behavior, metadata accumulation, cache hits and invalidations, LRU limits, non-cacheable policies, partial batch errors, wrappers, and invalid numeric input.

From PowerShell:

```powershell
& 'D:\Giochi\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\ecogo\Documents\Unreal Projects\Paradox\Paradox.uproject' `
  -unattended -nop4 -NullRHI -NoSound -NoSplash -DDC-ForceMemoryCache `
  '-ExecCmds=Automation RunTests EntityRelations.' `
  '-TestExit=Automation Test Queue Empty' -log
```

Expected invalid inputs remain observable. The duplicate-ID test declares its expected error log so the suite fails on unexpected errors while still verifying diagnostic severity.
