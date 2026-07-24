# Tactical Pause

Tactical Pause is a self-contained runtime plugin for authoritative gameplay pause and playback-speed control. It provides per-world state, safe C++ and Blueprint commands, configurable speed presets, event-driven UMG controls, console diagnostics, and ownership-aware teardown.

The first milestone targets single-player gameplay. The runtime module has no dependency on Paradox gameplay modules and does not add a global Tick.

## Enable and use

Enable `TacticalPause` in the project or Plugin Browser, then start a Game or PIE world with a local player controller. The plugin declares and enables its Common UI dependency. It does not add a widget to the screen by default: the project explicitly owns placement and lifetime of `WBP_TacticalPauseControls_Default` or a replacement widget.

`UTacticalPauseControlsWidget` derives from `UCommonActivatableWidget` and intentionally creates no layout or style in C++. Populate its Widget Blueprint yourself using Common UI button widgets with these exact variable names:

- `PlayButton`
- `PauseButton`
- `SpeedButton1`
- `SpeedButton2`
- `SpeedButton3`
- `SpeedButton4`

All six fields are required `meta=(BindWidget)` properties of type `UCommonButtonBase`. They may use any project-specific Blueprint subclass and `UCommonButtonStyle`. Speed buttons map to the first four validated presets in deterministic order. C++ owns click routing, enabled/selected state, visibility, activation, and delegate binding; the Widget Blueprint owns hierarchy, content, animations, and styling.

The default presets are:

| ID | Label | Multiplier |
| --- | --- | ---: |
| `Normal` | x1 | 1.0 |
| `Fast` | x1.5 | 1.5 |
| `Faster` | x2 | 2.0 |
| `Fastest` | x3 | 3.0 |

## Playback model

`UTacticalPauseWorldSubsystem` is the only authoritative temporal owner. Its states are `Playing`, `Paused`, `TransitioningToPlay`, and `TransitioningToPause`.

Selected speed and applied speed are intentionally separate:

- Selected speed is the speed used while playing or on the next resume.
- Applied speed is the world's current effective multiplier; it is reported as `0` while paused.
- Selecting another speed while paused does not resume the world. Play resumes at the new selected speed.

Pause and Play requests are idempotent. Every mutation returns `ETacticalPauseRequestResult`; invalid speed, missing preset, unsafe transition, unavailable world, external ownership conflict, and application failure remain observable through both the return value and `OnRequestFailed`.

## Settings

Settings are available under **Project Settings > Game > Tactical Pause**:

- `SpeedPresets`: ordered data-driven presets; IDs must be unique and multipliers finite and positive.
- `DefaultPresetId`: initial selected preset.
- `MaximumAllowedMultiplier`: plugin-level upper bound, additionally constrained by the world's Unreal time-dilation limit.
- `bAllowSpeedSelectionWhilePaused`: controls whether the next resume speed may be changed while paused.
- `bCreateDefaultWidgetAutomatically`: opts into local-player-owned UI creation; it is disabled by default.
- `DefaultWidgetClass`: soft class reference for a replacement widget.
- `WidgetZOrder`: player-screen Z order used only by automatic creation.
- `WidgetPlayerPolicy`: primary local player or every local player, used only by automatic creation.
- `bShowWidgetWhilePlaying` / `bShowWidgetWhilePaused`: state-based visibility.

Invalid preset entries are rejected independently. Duplicate IDs use the first valid entry, ordering is stable by `SortOrder`, and the first valid preset becomes the deterministic fallback when the configured default is unavailable. If none are valid, the subsystem supplies a safe x1 preset.

## Blueprint API

Use the nodes in the **Tactical Pause** category:

- `Get Tactical Pause Subsystem`
- `Pause Simulation`, `Play Simulation`, `Toggle Simulation Pause`
- `Set Simulation Playback Speed`, `Set Simulation Playback Preset`
- `Is Simulation Paused`
- `Get Selected Playback Speed`, `Get Applied Playback Speed`

For event-driven integration, bind to the world subsystem delegates:

- `OnPlaybackStateChanged`, `OnPaused`, `OnResumed`
- `OnSelectedPlaybackSpeedChanged`, `OnAppliedPlaybackSpeedChanged`
- `OnRequestFailed`

Do not call `SetPause` or `SetTimeDilation` from UI or gameplay code that uses this plugin; route commands through the subsystem so validation and restoration remain coherent.

## C++ API

Retrieve the subsystem from the relevant gameplay world and call its command/query surface:

```cpp
UTacticalPauseWorldSubsystem* TacticalPause = World->GetSubsystem<UTacticalPauseWorldSubsystem>();
if (TacticalPause)
{
    const ETacticalPauseRequestResult Result = TacticalPause->SetPlaybackPreset(TEXT("Faster"));
    if (Result == ETacticalPauseRequestResult::Succeeded)
    {
        TacticalPause->RequestPause();
    }
}
```

Include `Subsystems/TacticalPauseWorldSubsystem.h` and add `TacticalPause` to the consuming module's dependencies.

## Adding the widget manually

Manual ownership is the default and recommended integration. Create the controls from the local
PlayerController, HUD, or project Common UI root that owns the rest of the gameplay interface.

In Blueprint, for a simple player-screen integration:

1. Call **Create Widget** with `WBP_TacticalPauseControls_Default` (or your derived class).
2. Pass the local PlayerController as **Owning Player**.
3. Promote the result to a variable owned by that controller, HUD, or root widget.
4. Call **Add to Player Screen** with the desired Z order.
5. Call **Activate Widget** so the Common UI widget subscribes to TacticalPause events.
6. During teardown, call **Deactivate Widget**, then **Remove from Parent**, and clear the stored reference.

If the project already owns a dedicated `UCommonActivatableWidgetStack`, use its **Push Widget**
node with the Tactical Pause widget class instead. The container creates and activates the widget;
remove or deactivate it through that same Common UI navigation owner rather than also adding it to
the player screen.

Equivalent direct C++ ownership is:

```cpp
UPROPERTY(EditDefaultsOnly)
TSubclassOf<UTacticalPauseControlsWidget> TacticalPauseWidgetClass;

UPROPERTY(Transient)
TObjectPtr<UTacticalPauseControlsWidget> TacticalPauseWidget;

TacticalPauseWidget = CreateWidget<UTacticalPauseControlsWidget>(
    PlayerController,
    TacticalPauseWidgetClass);

if (TacticalPauseWidget && TacticalPauseWidget->AddToPlayerScreen(100))
{
    TacticalPauseWidget->ActivateWidget();
}
```

The owner must symmetrically call `DeactivateWidget`, `RemoveFromParent`, and clear its reflected
reference during teardown. Do not create the same controls through both a Common UI container and
`AddToPlayerScreen`.

## Building or replacing the widget

Open `WBP_TacticalPauseControls_Default`, or create another Widget Blueprint derived from `UTacticalPauseControlsWidget` and assign it to `DefaultWidgetClass`. Add the six required Common UI buttons above and mark them as variables with the exact names expected by `BindWidget`. The plugin does not construct, clear, or rearrange the Blueprint widget tree.

Configure each button's Common UI style and visual content in Blueprint. The native class marks state buttons and the selected speed slot through Common UI selection state; `On Tactical Pause Presentation Updated` is available for additional designer-side animation or text updates.

Keep temporal mutations routed through `RequestPauseFromWidget`, `RequestPlayFromWidget`, and `SelectPlaybackPresetFromWidget`. The base class subscribes only while active and remains polling-free.

To opt back into the plugin's legacy local-player-owned creation, enable
`bCreateDefaultWidgetAutomatically`. The local-player subsystem then loads `DefaultWidgetClass`,
adds it using `WidgetZOrder`, activates it, and removes it during teardown.

## External ownership and restoration

The plugin does not remove a pause it did not acquire. A pre-existing external pause is observed, and a Play request reports `ExternalStateConflict`.

Before the first plugin-owned time-dilation change, the subsystem snapshots the current external value. On teardown, it restores only values that are still plugin-owned. If another system changes dilation after the plugin, ownership is relinquished and the newer external value is preserved. A later explicit speed request may acquire the current value as a new baseline.

World end play and subsystem deinitialization reject new transitions, restore owned dilation, release owned pause, and unbind plugin-owned events. The local-player subsystem removes an opt-in automatic widget; a manually created widget remains the responsibility of its project owner.

## Paradox Gameplay Actions integration note

The current Paradox project adds `UTacticalPauseActionQueueComponent` as a project-side adapter
(a temporary architectural "giunta") between Tactical Pause and `UGameplayActionComponent`.
TacticalPause does not depend on this component and remains unaware of gameplay commands.

The adapter currently coordinates scheduler pause ownership and exposes one replaceable
"next action" slot. It does not implement a second scheduler: submitted requests still use the
native Gameplay Actions queue. This extra component is intentionally provisional. Once the desired
planning semantics are consolidated in GameplayActions, the Paradox integration can be simplified
or removed so commands use `UGameplayActionComponent` and its native Queue policy directly.

## Diagnostics

The module uses `LogTacticalPause`. These console commands operate on the current world:

```text
TacticalPause.Status
TacticalPause.Pause
TacticalPause.Play
TacticalPause.SetSpeed <Multiplier>
```

`TacticalPause.Status` reports state, selected/applied speed, selected preset, and validated preset count.

## Limitations and troubleshooting

- Single-player is the validated scope. The UI can be created for every local player, but pause and dilation remain global per world.
- Generic temporal participants/adapters are intentionally deferred until a concrete integration requires notification ordering and rollback semantics.
- The supplied Common UI contract exposes four speed slots. Presets after the fourth remain available through the subsystem API but need a custom Blueprint control calling `SelectPlaybackPresetFromWidget`.
- Systems using real time, unpaused ticking, audio-specific clocks, or custom simulation clocks may continue independently and need an owner-specific adapter.
- Queries read live Unreal state; ownership conflicts are detected on the next mutation request. There is no per-frame monitor.
- No widget is expected until the project adds one manually or enables automatic creation. For manual creation, verify the owning local PlayerController, player-screen/container insertion, and activation. For automatic creation, verify the opt-in setting, player policy, and configured soft class. In both cases compile the Widget Blueprint and confirm all six required `UCommonButtonBase` variables have exact `BindWidget` names.
- If Play returns `ExternalStateConflict`, another owner still holds pause or changed dilation. Resolve that owner instead of forcing Unreal temporal state directly.
- If a preset is absent, check `LogTacticalPause` for invalid/duplicate IDs and multiplier bounds.

Focused automation tests are available under `TacticalPause.Runtime`.
