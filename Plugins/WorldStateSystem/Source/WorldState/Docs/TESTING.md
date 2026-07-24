# WorldState automation coverage

Run the complete suite with:

```text
UnrealEditor-Cmd.exe Paradox.uproject -Unattended -NullRHI -NoSound -NoSplash -ExecCmds="Automation RunTests WorldState; Quit" -TestExit="Automation Test Queue Empty"
```

The 75 acceptance scenarios in `CODEX/WorldStateCore.md` are kept as focused assertions inside nine grouped Automation tests. Grouping keeps world setup reusable without hiding individual failures: each assertion has a scenario-specific message in the Automation report.

| Scenarios | Automation coverage |
|---|---|
| 1-5 | `WorldState.Runtime.Identity.RegistrationDuplicatesAndValidation` and `WorldState.Runtime.Existence.RespawnDestroyAttachmentReferencesAndTeardown`: registration, normal/PIE duplication, duplicate IDs, respawn identity, EndPlay and teardown. |
| 6-11 | Runtime validation plus `WorldState.Editor.PropertyPicker.BlueprintSourcesFilteringMissingDuplicatesAndTransactions`: Actor/Component roots, duplicates, removed source/property, class and canonical signature mismatches. |
| 12-25 | `WorldState.Runtime.Serialization.ValuesContainersReferencesAndIsolation`: scalar values, fixed arrays, native nested structs, arrays/sets/maps, and recursively rejected hard references. |
| 17-21, 34 | `WorldState.Editor.Serialization.TransientBlueprintUserDefinedStruct`: transient User Defined Struct round-trip and nested forbidden-reference diagnostics without tracked assets. |
| 26-34 | Serialization, baseline, required-reference and existence groups: soft object/class paths, optional/required resolution, compatible respawn and external different-path respawn. |
| 35-38 | Baseline and serialization groups: live/snapshot isolation, independent payload copies, immutable baseline and failed-capture publication safety. |
| 39-48 | Baseline, dependency and `WorldState.Runtime.Restore.PropertyFailurePolicy`: staged restore, deterministic topology, cycles, partial scope, unselected values, best-effort archive failure and reentrancy. |
| 49-53 | Existence group: snapshot-absent destruction, default/custom respawn, missing strategy preflight, authored Component reconstruction and registry mutation safety. |
| 54-61 | Baseline, existence and identity groups: complete relative transforms, rotation, Actor/attachment ordering, selected hierarchy, missing Scene Component, changed-parent warning and root-authority conflict. |
| 62-66 | Baseline, dependency and required-reference groups: participant callback timing, dependency completion and structured participant failure. |
| 67-75 | Baseline, dependency and required-reference groups: Started/ScopeResolved ordering, mutually exclusive exactly-once terminal events, rejected requests, stable session IDs, terminal stage/mutation context and nested global restore rejection. |

The editor picker test also exercises transactional mutation with Undo/Redo, duplicate presentation, Blueprint CDO/Simple Construction Script fallback and ambiguous multi-edit protection.
