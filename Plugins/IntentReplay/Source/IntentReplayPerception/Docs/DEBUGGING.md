# Debugging

## Gate

Il debug richiede entrambe le condizioni:

```text
IntentReplayPerception.Debug 1
bEnableDebug = true
```

Il controllo avviene prima di snapshot, risoluzione Entity ID, bounds e draw. Il componente non ha
Tick; usa un timer a bassa frequenza e primitive one-frame. Disabilitando il gate locale vengono
rimossi timer e registrazione HUD.

## Colori

- verde: matched;
- rosso: unexpected;
- arancione: ambiguous;
- viola: giustificazione verificata;
- azzurro: expected pending;
- verde scuro: expected consumed;
- grigio: inattivo.

I bounds sono leggermente più ampi di quelli di `PerceptionKnowledge`. Le linee partono dal
viewpoint del Listener. Gli Event mostrano la location; Hearing usa una sfera.

`DebugFilter` controlla State/Event, categorie di risultato, distance culling e dettaglio testo.
`BuildDebugFrame` restituisce una copia value-only e non muta track, consumption o journal.

## HUD runtime

L'HUD usa `UDebugDrawService`, disponibile nel modulo runtime, e mostra clock, stato comparison e
summary. La Milestone 2 non aggiunge un editor module né il Gameplay Debugger:

- l'output deve funzionare in build runtime;
- non è richiesta una dipendenza da `GameplayDebugger`;
- un frame value-only può essere consumato in futuro da tooling editor o da una categoria Gameplay
  Debugger separata.

## Log e dump

Categoria primaria:

```text
LogIntentReplayPerception
```

`DumpObservationTimelineToLog` riporta binding, sessioni, entry e summary. Warning/Error includono
owner, track/session e diagnostica strutturata.

`GetRuntimeStats` espone conteggi per recorded, compared, duplicate, matched, unexpected, ambiguous,
expected expired e costo dell'ultimo debug frame. Gli scope Unreal Insights principali sono:

```text
IntentReplayPerception_RecordObservation
IntentReplayPerception_FindCandidates
IntentReplayPerception_FinalizeTrack
IntentReplayPerception_BuildDebugSnapshot
```

## Risultati del Journal

- `Matched`: expected consumato;
- `UnexpectedObservation`: nessun candidato o discrepanza Event risolta;
- `UnexpectedStateValue` / `UnexpectedStateStatus`: State risolto e consumato;
- `Duplicate`: runtime Observation ID già visto, nessun consumo;
- `Ambiguous`: pareggio completo, nessun consumo;
- `IgnoredByPolicy`: confronto disabilitato o callback ridondante;
- `IgnoredWhilePaused`: pausa core/locale, nessun delegate Unexpected;
- `ComparisonUnavailable`: clock/sessione non accettano il confronto;
- `ExpectedRecordExpiredUnobserved`: expected scaduto senza side effect gameplay.

Usare `Reason`, `TimeDelta`, `bHasExpectedObservation` e `bConsumedExpectedRecord`; non fare parsing
delle stringhe diagnostiche.

## Troubleshooting

### Auto-start non parte

L'adapter deve essere inizializzato e bindato prima della transizione core a `Recording`. Il late
join viene rifiutato per default.

### Bundle invalid

Verificare formato Action Track `2`, Track ID, Recording Session ID e durata. Non accoppiare track
provenienti da recording diversi.

### Comparison unavailable

Preparare esattamente l'Action Track del bundle. Armare in `Ready`, oppure in `Playing` soltanto a
tempo zero/late join esplicitamente abilitato.

### Expected non osservati

Controllare Sense, identità persistente, tag, finestra temporale e tolleranze. Gli expected diventano
pending e poi expired; non vengono convertiti in eventi gameplay.

Con `bTreatPersistentStateObservationsAsOrderedSnapshots`, uno State expected della stessa chiave
non scade durante la comparison. Se una riacquisizione tardiva produce ancora
`NoCandidateInTimeWindow`, verificare che `bStrictPersistentIdentity` sia attivo e che Entity ID,
State Tag e Sense coincidano. Un cambio semantico deve invece apparire come
`UnexpectedStateValue` o `UnexpectedStateStatus`.

Se `bTreatVerifiedCausalEventsAsOccurrenceIdentity` è attivo, un Event con
`CorrelatedReplayIntent + Verified` non scade alla normale late tolerance: resta pending fino al
match o alla chiusura della comparison. Verificare `CurrentCorrelation.CausalRecordedIntentId` e
la correlazione dell'expected. Un valore assente indica che la Source non aveva una singola action
replay-owned `Running`; valori diversi indicano cause realmente differenti. `SourceMismatch`
continua a indicare un Entity ID diverso.

## Automation

```text
Automation RunTests IntentReplayPerception
Automation RunTests IntentReplay.
Automation RunTests PerceptionKnowledge.
```
