# EntityRelations

EntityRelations is a generic Unreal Engine 5.8 runtime plugin for answering directional questions between logical entities:

```text
Source --(Domain + Context)--> Target = Decision + Metadata + Status
```

It provides logical identity, a weak per-world registry, optional sparse directed state, data-driven policies, deterministic resolution, C++ and Blueprint queries, bounded caching, diagnostics, profiling, and Automation Tests. Query evaluation is synchronous and has no gameplay side effects.

The plugin intentionally contains no Paradox-specific temporal rules, perception implementation, cloning, paradox handling, replication, editor module, Tick, or asynchronous processing. Other modules provide such rules by subclassing a policy or supplying context.

## Enable and configure

1. Enable `EntityRelations` in the project. `Paradox.uproject` already enables it.
2. Create a Data Asset whose class is `Entity Relation Policy Set`.
3. Add instanced policies to its ordered `Policies` array. `Entity Relation Tag Query Policy` supplies a complete generic implementation.
4. Set the asset in **Project Settings > Game > Entity Relations > Default Policy Set**.
5. Add `Entity Identity Component` to every participating Actor. Add `Entity Relation State Component` only to Sources that need explicit directed state.

The default Policy Set soft reference is resolved once when each supported world subsystem initializes. A runtime override is scoped to one world and is released during world teardown. Without an active valid Policy Set, queries return `MissingPolicySet`; they never imply `Allow`.

## Identity lifecycle

`UEntityIdentityComponent` defaults to a runtime-generated ephemeral ID. The ID is created and registered at `BeginPlay`, then deregistered and cleared at `EndPlay`. Use explicit IDs for save-backed or externally assigned identities and configure them before registration.

Duplicate live IDs are rejected. The first registered entity remains authoritative. Identity and affiliation tags are mutated through controlled setters; their revision changes only when the value changes.

The subsystem exists only in `Game`, `PIE`, and `GamePreview` worlds. Constructors, CDOs, editor preview worlds, and design-time inspection do not register entities.

## Generic domains

The plugin owns only these Native Gameplay Tags:

- `Relation.Domain.General`
- `Relation.Domain.Interaction`
- `Relation.Domain.Damage`
- `Relation.Domain.Blocking`
- `Relation.Domain.VisualPerception`
- `Relation.Domain.AudioPerception`
- `Relation.Domain.GoalValidation`
- `Relation.Domain.TacticalPreview`

Game-specific modules own their classification, outcome, reason, identity, affiliation, state, and numeric context tags.

## Result contract

`FEntityRelationResult` separates technical status from semantic decision:

- `Status` reports whether the query was valid and evaluated.
- `Decision` is `NoOpinion`, `Allow`, or `Deny`.
- classification, outcome, and reason metadata explain or categorize the result.
- `WinningPolicyId` identifies the first policy that supplied a non-`NoOpinion` decision.

`Success + NoOpinion` is valid. Invalid IDs, invalid domains, non-finite numeric context, missing entities, missing configuration, unsupported domains, and policy failures remain observable as distinct statuses.

## Further reading

- [Architecture](ARCHITECTURE.md)
- [C++ API](CPP_API.md)
- [Blueprint API](BLUEPRINT_API.md)
- [Extending policies](EXTENDING.md)
- [Debugging, profiling, and tests](DEBUGGING.md)
