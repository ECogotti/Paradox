# Architecture

## Responsibilities

`UEntityIdentityComponent` owns a logical ID, debug name, identity tags, affiliation tags, and an identity revision. UObject address and Actor name are never the authoritative identity.

`UEntityRelationStateComponent` optionally owns sparse, asymmetric state:

```text
Source component
  TargetId A -> tags + numeric values + transient revision
  TargetId B -> tags + numeric values + transient revision
```

Empty entries are removed. State for `A -> B` never creates or changes `B -> A`. Persisted tags and numeric values use `SaveGame`; revisions and cache data are transient and reconstructed during the session.

`UEntityRelationPolicySet` is a Data Asset that owns instanced policies. Policies are treated as immutable configuration during normal runtime. `UEntityRelationsWorldSubsystem` validates and sorts enabled applicable policies by descending priority, using serialized array index as the deterministic tie-breaker.

## Evaluation flow

For every query the subsystem:

1. verifies Game Thread, IDs, domain, and finite numeric context;
2. resolves Source and Target through the weak registry;
3. rejects a missing or invalid Policy Set;
4. snapshots identity, affiliation, directed state, revisions, and borrowed Actor references;
5. checks the bounded cache when the request and all applicable policies allow it;
6. evaluates policies in deterministic order;
7. accumulates unique classification, outcome, and reason tags;
8. keeps the first non-`NoOpinion` decision authoritative;
9. continues metadata collection until a policy requests stop;
10. optionally stores the result and emits explicit diagnostic drawing.

Policy contexts are read-only snapshots. Their Actor pointers are borrowed for the duration of one synchronous call and must not be retained or mutated by policies.

## Cache and invalidation

The cache key contains Source ID, Target ID, domain, deterministic hashes of context tags and numeric context, Source revision, Target revision, pair revision, and Policy Set revision.

Ordinary identity and state changes invalidate by revision. Removing a state entry, deregistering an entity, replacing a reused ID, changing the Policy Set, or explicit cache clearing purges the relevant entries. This prevents stale results when a logical ID is reused.

The LRU is enabled by default and limited to 1024 entries. It is bypassed for explanations, `bAllowCache=false`, disabled global cache, a zero limit, or any applicable non-cacheable policy.

## Ownership and shutdown

- Actors own identity and state components.
- Policy Set Data Assets own their inline policy UObjects through `UPROPERTY(Instanced)`.
- The world owns the subsystem and its strong reference to the active Policy Set.
- The registry holds weak component references and does not keep Actors alive.
- registration and deregistration are symmetric at `BeginPlay` and `EndPlay`;
- module console commands are registered and removed symmetrically;
- per-world overrides, cache entries, registry entries, and delegates are cleared at subsystem shutdown.

There is no Tick, latent request, async task, or editor-only dependency in the runtime module.

## Observation events

Blueprint multicast events and native multicast delegates cover registration, deregistration, identity changes, directed-state changes, pair/entity invalidation, and active Policy Set changes. Notifications occur after effective changes only. Observers must not assume that a notification grants mutable access to subsystem internals.
