# IntentReplayPerception

`IntentReplayPerception` è il modulo runtime opzionale che registra e confronta le osservazioni
semantiche prodotte da `PerceptionKnowledge` sul clock autorevole di `IntentReplay`.

Il modulo non modifica il Knowledge Store e non interpreta le osservazioni come comandi gameplay.
Mantiene separati:

- Action Replay Track;
- Observation Track;
- Execution Journal;
- Observation Journal;
- Knowledge Store del Listener.

## Dipendenze

```text
IntentReplayPerception
├── IntentReplay
└── PerceptionKnowledge
```

Il modulo core `IntentReplay` non contiene include o dipendenze AI. L'adapter non dipende da codice
Paradox-specifico.

## Componente principale

Aggiungere `UIntentReplayObservationComponent` all'Actor che coordina il replay. Sono necessari:

- un `UIntentReplayComponent`;
- un `UPerceptionKnowledgeListenerComponent`;
- policy di recording e matching, con implementazioni native predefinite.

Il componente cerca soltanto componenti sullo stesso owner. Per il caso Player Controller/Pawn,
assegnare esplicitamente `IntentReplaySourceOverride` al componente sul Pawn e
`PerceptionListenerOverride` al Listener sul Controller. Non vengono eseguite world scan.

## Recording

Con `bAutoStartObservationRecording`, la sessione parte sulla transizione core `Created ->
Recording`, se i binding erano già validi. Il late join è rifiutato. Ogni osservazione accettata usa
`CaptureRecordingTimelinePoint`, quindi Action ed Observation Track condividono tempo relativo e
allocatore `TimelineSequence`.

La policy registra:

- primo stato;
- cambi semantici;
- `Unknown` e `Invalidated`;
- riacquisizioni in un nuovo perception epoch;
- ogni Event con Observation ID distinto.

Callback di stato identiche nello stesso epoch e Event con runtime Observation ID duplicato vengono
filtrati. Le Gameplay Tag Query per State, Event, Sense e Cause sono indipendenti.

Quando il core entra in `Draining` o `Finalized`, l'adapter smette subito di accettare callback. Un
Observation Track viene pubblicato soltanto dopo la validazione dell'Action Track formato `2`.
Cancel e failure core scartano la sessione non accoppiabile.

## Comparison

La comparison va armata dopo `PrepareReplay`, mentre il core è `Ready`, e prima di `StartReplay`.
È consentito armarla in `Playing` soltanto a tempo zero, salvo opt-in `bAllowLateJoin`.

Il matching usa indici per State (`Entity ID + State Tag + Sense`) ed Event
(`Event Tag + Source Entity ID + Sense`), ricerca temporale e scoring deterministico. Una
discrepanza risolta consuma il record atteso; `Duplicate` e `Ambiguous` non consumano. Con punteggio
completo uguale il default produce `Ambiguous`; disabilitando
`bReportEquivalentBestCandidatesAsAmbiguous` viene scelta la sequence più bassa.

L'opt-in `bTreatPersistentStateObservationsAsOrderedSnapshots`, valido con identità persistente
stretta, tratta gli State con la stessa chiave (`Entity ID + State Tag + Sense`) come una sequenza
ordinata. Gli expected restano pending fino al consumo o alla fine della comparison. Una
riacquisizione Sight tardiva confronta quindi status e valore con il prossimo snapshot registrato,
invece di diventare `NoCandidateInTimeWindow`. L'opzione generica è disabilitata; Paradox la
abilita per i cloni.

Il resolver nativo associa inoltre un Event alla singola action replay-owned attiva sulla Source,
quando questa espone una correlazione `IntentReplay.Correlation.RecordedIntent` non ambigua.
L'opt-in `bTreatVerifiedCausalEventsAsOccurrenceIdentity` usa tale `RecordedIntentId` verificato
come identità dell'occorrenza: Source ID, Event Tag, Sense, Cause, Instigator e consumo one-to-one
restano stretti, mentre drift di tempo, posizione, strength e loudness dovuto a interruzioni e
ricalcolo del replay non crea un falso Event nuovo. I record correlati restano pending fino al
match o alla fine della comparison. L'opzione è disabilitata nel plugin generico; Paradox la
abilita sul coordinatore dei cloni.

Pause e resume core vengono seguiti automaticamente. Le osservazioni durante pausa sono
`IgnoredWhilePaused`, senza consumo e senza delegate Unexpected. La pausa locale sospende il
confronto mentre il clock core continua.

## Output

`UIntentReplayObservationTrack` e `UIntentReplayTimelineBundle` sono transient, finalizzati e
immutabili. `UIntentReplayObservationJournal` è mutable, bounded per default, e conserva conteggi
cumulativi anche quando il ring buffer rimuove entry vecchie. Tutte le query collection restituiscono
copie.

Delegate Blueprint e native coprono:

- observation recorded;
- observation track finalized;
- observation compared, matched, unexpected e ambiguous;
- observation journal completed.

Vedere [SETUP.md](SETUP.md), [ARCHITECTURE.md](ARCHITECTURE.md) e
[DEBUGGING.md](DEBUGGING.md).
