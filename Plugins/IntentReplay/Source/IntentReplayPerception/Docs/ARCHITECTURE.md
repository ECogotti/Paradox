# Architettura

## Ownership e lifetime

- `UIntentReplayObservationComponent` possiede le sessioni mutable di recording/comparison e le
  istanze delle policy.
- `UIntentReplayObservationRecordingSession` esiste soltanto durante un recording sincronizzato.
- `UIntentReplayObservationComparisonSession` trattiene bundle, indici, consumed set, dedup set e
  journal della singola comparison.
- `UIntentReplayObservationTrack` e `UIntentReplayTimelineBundle` sono creati nel transient package,
  fuori dall'Actor sorgente.
- `UIntentReplayObservationJournal` appartiene alla comparison session.

Tutti i binding UObject persistenti sono `UPROPERTY`/`TObjectPtr`. Delegate e timer vengono rimossi
simmetricamente in unbind, uninitialize, EndPlay, distruzione e world teardown.

## Timeline

Il modulo non possiede un clock. Usa soltanto:

- `CaptureRecordingTimelinePoint(RecordingSessionId)`;
- `CapturePlaybackTimelinePoint(PlaybackSessionId)`;
- gli snapshot clock core;
- il lifecycle generico core.

Questo garantisce l'ordinamento cross-channel:

```text
RelativeTime -> TimelineSequence -> TrackSequence
```

Il bundle valida Track ID, Recording Session ID, formato core `2` e durata. Un Action Track legacy
formato `1` resta riproducibile dal core ma non può creare un bundle percettivo.

## Payload

State ed Event hanno payload registrati distinti:

- State: Entity ID, State Tag, valore tipizzato, status, Sense, confidence, posizione e timestamp;
- Event: Observation ID sorgente, Event Tag, Source/Instigator ID, Sense, posizione, strength,
  loudness, confidence, Cause Tag e timestamp.

Il wrapper ha un discriminante esplicito. `Known + Bool(false)` non equivale a `Unknown`. Nessun
payload conserva Actor, Component o altre reference runtime.

## Matching

Gli indici primari restringono per identità/tag/sense; quelli secondari producono motivi diagnostici
precisi. Gli array indicizzati sono già ordinati per tempo e la ricerca parte dal lower bound della
finestra configurata.

Default:

- State: `±0.25 s`;
- Hearing: `±0.50 s`, posizione `100 cm`;
- altri Event: `±0.25 s`, posizione `100 cm`;
- float `1e-4`;
- vector `1 cm`;
- strength/loudness `0.1`;
- identità persistente stretta;
- posizione e confidence degli stati non vincolanti.

Lo scoring considera correlazione esplicita, status/valore, instigator, Cause Tag, posizione,
strength/loudness e tempo. La sequence ordina sempre candidati e diagnostica, ma non nasconde un
pareggio completo: il default restituisce `Ambiguous`.

Una selezione risolta viene consumata anche quando il risultato è
`UnexpectedStateValue`, `UnexpectedStateStatus` o un'altra discrepanza. Duplicati e ambiguità non
consumano. Gli expected scaduti sono soltanto diagnostica; non producono reazioni gameplay.

Con `bTreatPersistentStateObservationsAsOrderedSnapshots` e
`bStrictPersistentIdentity`, gli State della stessa chiave persistente non scadono nella normale
finestra temporale. Ogni callback consuma il primo snapshot non consumato in
`TimelineSequence`, poi confronta status, valore e le tolleranze State abilitate. Questo preserva
l'ordine delle transizioni e consente a una riacquisizione Sight ritardata dal movimento di
produrre `UnexpectedStateValue` o `UnexpectedStateStatus`. Gli snapshot mai riacquisiti scadono
alla chiusura della comparison.

## Correlazione

`ResolveObservationCorrelation` è un `BlueprintNativeEvent`. Il resolver predefinito non deduce
causalità dalla vicinanza temporale. I dati Source, Instigator e Cause restano nel payload. La
giustificazione `ObserverCaused` viene impostata quando l'Entity ID dell'instigator coincide con
quello risolto per l'observer.

Per un Event prodotto da un'altra Source, il resolver usa il registry
`PerceptionKnowledge` per raggiungere l'Actor sorgente e accetta una causalità replay soltanto se:

- la Source possiede una playback session Intent Replay attiva;
- esiste una sola action replay-owned `Running` che espone
  `IntentReplay.Correlation.RecordedIntent`;
- il GUID della correlazione è valido.

Il risultato è `CorrelatedReplayIntent + Verified`. Più intenti causali distinti sono ambigui e
non vengono indovinati.

Con `bTreatVerifiedCausalEventsAsOccurrenceIdentity`, un confronto esatto del Recorded Intent ID
seleziona in ordine di `TimelineSequence` il primo record non consumato con lo stesso Source ID,
Event Tag e Sense. Cause e Instigator restano vincolanti. Tempo, posizione, strength e loudness
sono attributi diagnostici dell'occorrenza, non una nuova identità; l'expected viene differito
dalla normale expiration e scade soltanto al completamento se non è mai stato osservato. Una
seconda emissione non trova un secondo record e rimane `UnexpectedObservation`.

## Estensione

Sostituire `RecordPolicyClass` per il filtro semantico e `MatchPolicyClass` per l'equivalenza dei
valori. Indici, consumo, lifecycle, immutabilità e tie-breaking restano invarianti native.

Non usare il modulo per GOAP, Intent prediction, persistenza o replication. Queste integrazioni
possono consumare bundle/journal in moduli separati senza accoppiare il core all'AI.
