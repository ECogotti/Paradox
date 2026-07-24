# TacticalPause runtime module

`TacticalPause` is a runtime module with one temporal authority and an optional presentation owner:

- `UTacticalPauseWorldSubsystem` owns temporal state once per Game or PIE world.
- `UTacticalPauseLocalPlayerSubsystem` owns widget creation only when the project explicitly enables the automatic-creation setting, which is disabled by default.

The widget and Blueprint library only forward commands to the world subsystem. `FUnrealTacticalPauseTemporalDriver` is private and isolates the verified Unreal calls (`APlayerController::SetPause` and `AWorldSettings::SetTimeDilation`) from the public API.

## Public surface

Public headers are grouped by responsibility:

- `Types/TacticalPauseTypes.h`: state, result, request, policy, preset, and event payload types.
- `Settings/TacticalPauseSettings.h`: project-wide data-driven defaults.
- `Subsystems/TacticalPauseWorldSubsystem.h`: authoritative commands, queries, and Blueprint/native delegates.
- `Subsystems/TacticalPauseLocalPlayerSubsystem.h`: opt-in automatic widget ownership and query.
- `Blueprint/TacticalPauseBlueprintLibrary.h`: world-context forwarding nodes.
- `Widgets/TacticalPauseControlsWidget.h`: Common UI activation, required button bindings, command routing, and presentation hooks.
- `TacticalPause.h`: module log category and scoped logging macros.

Mutation commands return `ETacticalPauseRequestResult`. Callers should handle success, idempotency, validation failure, transition re-entry, external conflict, apply failure, and shutdown explicitly. Native observers can use the `On...Native()` accessors; Blueprint observers use the matching assignable properties.

## State and ownership rules

Pause ownership is represented by Unreal's `FCanUnpause` delegate. The module releases only its own pause. Time-dilation ownership records the pre-acquisition value and the last value written by the plugin. Restoration occurs only when the live value still matches that last write.

The subsystem has no Tick. It synchronizes observed Unreal state on relevant public operations, emits state/speed events, and performs symmetric restoration in both `OnWorldEndPlay` and `Deinitialize`.

Preset validation occurs per world during initialization. Valid presets have a non-empty unique ID and a finite positive multiplier within the configured limit; mutation commands also enforce the world's engine limit.

## Common UI contract

`UTacticalPauseControlsWidget` derives from `UCommonActivatableWidget` and never constructs a widget tree. Its Widget Blueprint must supply these `UCommonButtonBase` variables using exact required `meta=(BindWidget)` names:

- `PlayButton`
- `PauseButton`
- `SpeedButton1`
- `SpeedButton2`
- `SpeedButton3`
- `SpeedButton4`

The four speed fields represent validated preset indices 0-3. The native class binds clicks, drives Common UI interaction and selection state, observes the subsystem only while activated, and calls `On Tactical Pause Presentation Updated` after a refresh. Blueprint remains responsible for all layout, visual content, button styles, and animations. Keep command routing in the base API; do not mutate world pause or dilation from the widget.

The module does not place this widget on screen by default. A project UI owner should create it with
the local PlayerController, retain it through a reflected reference, add it to the player screen and
call `ActivateWidget`. Cleanup must call `DeactivateWidget` and `RemoveFromParent`. A project using a
dedicated `UCommonActivatableWidgetStack` may instead push the widget class and leave creation,
activation, and removal to that container. Automatic local-player ownership remains available only
as an explicit opt-in through `bCreateDefaultWidgetAutomatically`.

## Module dependencies

Public dependencies are `Core`, `CoreUObject`, `Engine`, `DeveloperSettings`, `CommonUI`, and `UMG`. The plugin descriptor declares Common UI explicitly. Runtime code contains no editor-only, direct Slate, or Paradox-specific dependency.

## Tests

Development automation tests live in `Private/Tests/TacticalPauseTests.cpp` under the `TacticalPause.Runtime` prefix. They use an injected private temporal driver and focused test worlds to validate transitions, validation, presets, external ownership/restoration, Common UI inheritance/BindWidget metadata, preset-slot mapping, and command routing. The designer asset is not loaded by automation until its required buttons have been placed and compiled.

Optional generic participant registration is not part of this milestone. Add it only with a concrete adapter requirement and defined ordering, ownership, failure reporting, and rollback behavior.

The Paradox project currently supplies `UTacticalPauseActionQueueComponent` as a provisional
project-side adapter (an architectural "giunta") for Gameplay Actions pause ownership and a
replaceable next-action slot. It is not part of this runtime module and does not replace the
Gameplay Actions scheduler: requests still enter its native queue. A future integration may remove
the adapter and use `UGameplayActionComponent` plus its native Queue policy directly once the final
planning semantics have been established.
