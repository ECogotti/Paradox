# Extending EntityRelations

## Native policy

Subclass `UEntityRelationPolicy` and override the const native implementation:

```cpp
FEntityRelationContribution UMyRelationPolicy::EvaluateRelation_Implementation(
    const FEntityRelationPolicyContext& Context) const
{
    FEntityRelationContribution Contribution;
    if (CanPerformRule(Context))
    {
        Contribution.Decision = EEntityRelationDecision::Allow;
        Contribution.ReasonTags.AddTag(MyTags::Reason_AllowedByRule);
    }
    return Contribution;
}
```

Configure `PolicyId`, priority, supported domains, cacheability, enablement, and stopping behavior on the inline policy instance in the Policy Set. The stable public evaluator validates the context and owns result assembly; subclasses return only their contribution.

## Blueprint policy

Create a Blueprint class derived from `Entity Relation Policy` and implement **Evaluate Entity Relation**. The native base remains responsible for boundary validation. Return `NoOpinion` with no metadata when the policy does not apply.

Do not mutate Actors, components, subsystems, or global state from evaluation. Do not retain the Source/Target Actors or query snapshots. Mark a policy non-cacheable when it reads any value not represented by identity revisions, pair revision, query context, or Policy Set revision.

## Choosing data location

- identity/affiliation tags describe one entity and invalidate all relations involving it;
- directed state describes a persisted Source-owned fact about one Target;
- query context describes facts supplied for one request;
- policy configuration describes immutable rules shared by the world;
- classification/outcome/reason tags describe a result and do not mutate gameplay state.

External modules should own domain-specific tags and policies. For example, a temporal module may add a policy for a generic plugin domain, but EntityRelations itself must not depend on or encode temporal mechanics.

## Determinism and stopping

Larger priorities evaluate first. Equal priorities follow serialized Policy Set order. The first non-`NoOpinion` decision wins permanently, while later policies may append metadata. A contribution may set `bStopEvaluation`; the configured stop-after-contribution option stops only when that policy actually contributed.

Use stopping for an intentional boundary, not merely to avoid evaluating a policy that should instead have a narrower supported domain or tag query.

## Validation

Policy Set validation rejects null policies, missing or duplicate IDs, missing domains, and sets with no enabled policy. Disabled policies are reported as information; duplicate enabled priorities are warnings because array order remains deterministic. Invalid Policy Sets cannot become active at runtime.
