# Runtime debugging

The Hearing Range renderer receives global debug CVar changes immediately. With local debug
enabled, `PerceptionKnowledge.Debug 0` hides diagnostic presentation without polling.
`bVisibleInGameplay` is an independent presentation path.

For a Controller-owned renderer, inspect `GetRendererDiagnostic` and
`GetActiveRenderComponent`: the authoring component remains on the hidden Controller, while the
actual transient primitive must be owned by the possessed Pawn. Missing listener, Body Actor,
effective range, mesh, visibility policy, or proxy creation is reported explicitly.

## Enablement

Visual debug uses the mandatory two-part gate:

```text
PerceptionKnowledge.Debug = 1
AND
component bEnableDebug = true
```

The global console variable disables all plugin drawing immediately. Source and Listener flags are disabled by default and can be enabled per instance. Drawing runs on a low-frequency timer and uses one-frame primitives; Source, Listener, and subsystem have no default Tick.

Listener filters control Sight, Hearing, state labels, recent events, known-but-not-perceived Sources, text, bounds, lines, maximum displayed state count, and an optional Source Entity ID.

## Colors

- Magenta: invalid configuration, invalid identity, rejected/unresolved semantic data, or Hearing correlation failure.
- Cyan: Source currently perceived through Sight.
- Yellow: Source/event perceived through Hearing or uncertain information.
- Gray: known Source not currently perceived.
- Blue: registered Source not currently perceived.
- White: Listener, body/viewpoint, and neutral information.

Green, red, orange, and purple comparative timeline meanings are intentionally absent. They require IntentReplay context.

## What is drawn

Sources show Actor bounds, Actor name, abbreviated Entity ID, configured senses, native and semantic registration, and exposed state count.

Listeners show the actual Body Actor, viewpoint, forward direction, Sight/Lose Sight radii and FOV directions, Hearing range, knowledge revision, known states, recent event count, and current sense relationships.

State labels can include semantic status, sense, confidence, age, fact revision, and Listener-wide revision. Hearing events show location, Listener line, tag, source/instigator IDs, loudness, strength, age, and correlation status. Correlation failures draw magenta.

`BuildDebugFrame` separates value gathering from drawing. With either debug gate disabled it returns before building expensive Source/event data. This contract is covered by automation.

## Text diagnostics and stats

Call:

- `DumpSourceToLog`;
- `DumpKnowledgeToLog`;
- `UPerceptionKnowledgeWorldSubsystem::DumpRegistryToLog`;
- `GetRuntimeStats`.

Runtime stats include registered Source/Listener counts, pending noises, produced observations, duplicate observations suppressed by anti-spam, visible refresh count/time, and debug frame count/cost.

Unreal Insights scopes cover:

- `PerceptionKnowledge_ProcessSightStimulus`;
- `PerceptionKnowledge_ProcessHearingStimulus`;
- `PerceptionKnowledge_RefreshVisibleSourceStates`;
- `PerceptionKnowledge_BuildKnowledgeSnapshot`;
- `PerceptionKnowledge_DrawDebug`.

## Gameplay Debugger decision

Milestone 1 deliberately has no editor module and no Gameplay Debugger category. A category would add editor/debugger build dependencies to a standalone runtime milestone, while the required spatial and textual diagnostics are already available through runtime drawing, dumps, stats, and Insights. A later comparative-debug milestone can add a separate optional debugger module when IntentReplay supplies timeline classifications.

## Troubleshooting

- Missing Profile: the Listener logs an error and remains suspended.
- No possessed Pawn on a Controller: Body/viewpoint are invalid and observation production is suspended; knowledge is preserved.
- Duplicate Entity ID: the second Source is rejected and the first remains registered.
- Direct Hearing event rejected: call `EmitSemanticNoise`.
- Missing Hearing correlation: ensure sound emission goes through the Source wrapper and that correlation TTL/capacity settings are sufficient.
- State type mismatch: keep one stable value type per State Tag.
