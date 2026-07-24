# C++ API

## Query by Actor

Add `EntityRelations` to the consuming module's dependency list, then include the public headers you use:

```cpp
#include "EntityRelationTags.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"

UEntityRelationsWorldSubsystem* Relations =
    GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>();

FEntityRelationQueryContext Context;
Context.Domain = EntityRelationTags::Domain_Interaction;
Context.bAllowCache = true;

const FEntityRelationResult Result =
    Relations->EvaluateRelationByActor(SourceActor, TargetActor, Context);

if (Result.Status == EEntityRelationQueryStatus::Success)
{
    // NoOpinion, Allow, and Deny are all semantic outcomes of a valid query.
}
```

Equivalent entry points accept IDs or identity components. `EvaluateRelationsFromSource` accepts ordered Target IDs and returns exactly one independent result per input in the same order.

## Query context

`FEntityRelationQueryContext` contains:

- one required valid domain;
- unordered context tags;
- finite numeric values keyed by Gameplay Tag;
- `bRequestExplanation`, which captures per-policy trace and bypasses cache;
- `bAllowCache`, which lets the caller bypass cache without changing project settings.

Context is request data, not mutable world state. Use it for facts supplied by the caller, such as the attempted interaction type or tactical mode. Use directed state for explicit persisted facts owned by a Source about a Target.

## Directed state

```cpp
#include "Components/EntityRelationStateComponent.h"

const FEntityRelationStateMutationResult Change =
    SourceState->AddStateTagForTarget(TargetId, MyTags::Relationship_Trusted);

if (Change.Status == EEntityRelationStateMutationStatus::Changed)
{
    // Revision and pair invalidation were published after the mutation.
}
```

Use the controlled set/add/remove/clear operations. They distinguish `Changed`, `Unchanged`, invalid input, missing state, and missing registration. Numeric values must be finite. The returned state from `GetStateForTarget` is a copy; callers cannot mutate internal map entries.

## Identity

Explicit IDs must be valid and assigned before the component owns a live registry entry. Runtime-generated IDs are ephemeral and are unsuitable as persistent save identifiers. Tag and debug-name setters return `false` when the requested value is invalid or unchanged.

`FEntityRelationId` is Blueprint-compatible, hashable, GUID-backed, and opaque at API boundaries. `ToString()` is intended for logs and console commands, not identity comparison.

## Runtime configuration

Use `SetPolicySetOverride` for a loaded, valid Policy Set needed by one world. The subsystem rejects null or invalid overrides. `ClearPolicySetOverride` restores the default asset resolved during subsystem initialization; it performs no synchronous asset load.

The active configuration is per-world. Never store a subsystem or borrowed policy context across world teardown.

## Native observation

The subsystem exposes native delegates for entity registration/deregistration, identity changes, directed-state changes, entity/pair invalidation, and Policy Set changes. Bind and unbind within a lifecycle that cannot outlive the world. Dynamic equivalents are available for Blueprint consumers.

## Failure handling

Always inspect `Status` before interpreting `Decision`. `NoOpinion` is not an error and an error is never converted to `Allow`. Public mutations and registration return structured status plus contextual message where a failure may occur.
