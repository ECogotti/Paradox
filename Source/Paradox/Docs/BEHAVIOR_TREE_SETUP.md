# Behavior Tree and Blackboard setup

Milestone 3 provides native nodes and validation, not `.uasset` files. Missing or invalid assets
leave the clone stationary and emit an actionable `LogParadox` error.

## Blackboard

Create a Blackboard and add these names with these exact native types:

| Key | Type |
|---|---|
| `BehaviorMode` | Enum (`EParadoxCloneBehaviorMode`) |
| `InvestigationLocation` | Vector |
| `InvestigationSourceActor` | Object, base class Actor |
| `InvestigationSourceEntityId` | String |
| `InvestigationJournalEntryId` | String |
| `InvestigationObservationType` | Enum (`EPerceptionKnowledgeObservationType`) |
| `InvestigationSemanticTag` | Name |
| `InvestigationSense` | Name |
| `LastModeTransitionReason` | Name |
| `InvestigationResponseRuleId` | Name |
| `InvestigationPriority` | Int |
| `InvestigationRevision` | Int |
| `HasValidInvestigation` | Bool |
| `ReplayResumeAvailable` | Bool |
| `InvestigationConfidence` | Float |

The clone controller validates every key after `RunBehaviorTree`. Do not write these keys from
Blueprint services: they are coordinator mirrors.

## Tree graph

Create a Behavior Tree using that Blackboard:

```text
Root
└── Selector
    ├── Sequence: Investigating
    │   ├── Blackboard decorator: BehaviorMode == Investigating
    │   │   Observer Aborts = Both
    │   └── Paradox Investigate Observation
    ├── Sequence: Replay
    │   ├── Blackboard decorator: BehaviorMode == Replay
    │   │   Observer Aborts = Both
    │   └── Paradox Run Intent Replay
    └── Sequence: GOAP
        ├── Blackboard decorator: BehaviorMode == Goap
        │   Observer Aborts = Both
        └── Paradox GOAP Placeholder (Inert)
```

The three native tasks are node-instanced, bind exact native delegates, and remove bindings
symmetrically on finish/abort. The Investigating task remains latent across retargets. The GOAP
placeholder deliberately never performs planning; a real handoff stops the tree before external
notification.

Assign the Behavior Tree to `CloneBehaviorTree` on the `AParadoxCloneController` Blueprint/CDO.
Assign a Perception profile and, optionally, project response/recovery Data Assets on the relevant
components. The time loop starts the tree only after Intent Replay and observation comparison are
ready.
