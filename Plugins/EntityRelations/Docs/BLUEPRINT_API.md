# Blueprint API

## Components

Add **Entity Identity Component** to Source and Target Actors. Its default mode creates an ephemeral ID at Begin Play. Select explicit mode only when the ID is authored or restored before registration.

Add **Entity Relation State Component** to a Source only when it must store directed tags or numeric values for specific Target IDs. Use its callable functions to mutate state; do not treat a returned state struct as a live mutable reference.

## Policy Set setup

Create a Data Asset of class **Entity Relation Policy Set**. Add inline policies to the ordered array and configure for every policy:

- stable, unique Policy ID;
- priority, where larger values run first;
- one or more supported domains;
- enabled and cacheable flags;
- optional stop-after-contribution behavior.

The generic **Entity Relation Tag Query Policy** matches Source identity/affiliation tags, Target identity/affiliation tags, directed-state tags, and request context tags. Empty tag queries match all, so it can also act as a domain fallback. Configure its decision and metadata contribution.

Assign the Policy Set in **Project Settings > Game > Entity Relations**. A missing asset produces `MissingPolicySet`.

## Single query

Use the non-pure **Evaluate Relation** Blueprint node with:

- a world context;
- Source Actor;
- Target Actor;
- a query context containing a valid domain.

The `Success` output is true only when result status is `Success`. Branch on result `Decision` separately so that `NoOpinion` is preserved.

## Batch query

Use **Evaluate Relations From Source** with one Source and an ordered Target Actor array. The returned array has the same length and order. A null Actor, missing identity, or unregistered Target affects only its corresponding result.

## Pure helpers

Pure, inexpensive nodes provide:

- identity-component lookup;
- GUID/Entity ID conversion and diagnostic ID generation;
- exact or hierarchical classification/outcome tag checks;
- localized status and decision text.

Queries are intentionally non-pure because they update cache statistics and may emit explicitly enabled diagnostics.

## Designer safety

Blueprint policies receive copies of identity, affiliation, directed state, and query context. Source/Target Actors are borrowed only during the synchronous event. Do not store them for later use. Blueprint policies are appropriate for customization and content logic, but native C++ policies are preferred for very large per-frame batches.
