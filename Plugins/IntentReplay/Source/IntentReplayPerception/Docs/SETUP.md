# Setup

## Same owner

In Blueprint aggiungere allo stesso Actor:

1. `Gameplay Action Component`;
2. `Intent Replay Component`;
3. `Perception Knowledge Listener Component`, configurato come descritto nel plugin
   `PerceptionKnowledge`;
4. `Intent Replay Observation Component`.

Se gli override non sono assegnati, l'adapter usa same-owner discovery. La produzione di
osservazioni dipende comunque dal Listener: profilo mancante, Body/Viewpoint invalido o Listener
sospeso non vengono aggirati.

## Player Controller e Pawn

Per un player top-down il Listener può appartenere al Player Controller mentre Action/Replay e
l'adapter appartengono al Pawn:

```cpp
ObservationAdapter->SetIntentReplaySource(PawnReplayComponent);
ObservationAdapter->SetPerceptionKnowledgeListener(ControllerListenerComponent);

const FIntentReplayObservationOperationResult Result =
    ObservationAdapter->InitializeObservationReplay();
```

Entrambi devono appartenere allo stesso World. Un binding non può essere sostituito durante una
sessione recording/comparison non terminale.

## Recording C++

Registrare/bindare l'adapter prima di chiamare `StartRecording` sul core:

```cpp
FIntentRecordingStartResult CoreStart =
    ReplayComponent->StartRecording(FIntentRecordingOptions());

// Con bAutoStartObservationRecording=true non serve una seconda start.
```

Per controllo manuale disabilitare l'auto-start e chiamare
`StartSynchronizedObservationRecording` a tempo zero. `StopObservationRecording` chiude
l'accettazione locale; il track viene comunque pubblicato soltanto quando il core finalizza.

Usare `OnObservationTrackFinalized`:

```cpp
void UResetCoordinator::HandleBundleReady(
    UIntentReplayObservationTrack* ObservationTrack,
    UIntentReplayTimelineBundle* Bundle)
{
    PendingBundle = Bundle;
}
```

## Trasferimento durante il reset

Il bundle è transient. Deve essere trattenuto da un oggetto che sopravvive al reset:

```cpp
UPROPERTY(Transient)
TObjectPtr<UIntentReplayTimelineBundle> PendingBundle;
```

Una variabile raw non protegge bundle e track dal Garbage Collector.

Sul clone:

```cpp
const FIntentReplayPrepareResult Prepare =
    CloneReplay->PrepareReplay(
        PendingBundle->GetActionTrack(),
        FIntentReplayPlaybackOptions());

if (Prepare.Status == EIntentReplayPrepareStatus::Ready)
{
    FIntentReplayObservationMatchOptions MatchOptions;
    const FIntentReplayObservationOperationResult Armed =
        CloneObservations->StartObservationComparison(PendingBundle, MatchOptions);

    if (Armed.Succeeded())
    {
        CloneReplay->StartReplay();
    }
}
```

Se `PrepareReplay` restituisce `Preparing`, attendere `OnReplayPrepared`, armare la comparison in
`Ready`, quindi avviare il replay.

## Blueprint

Workflow recording:

1. inizializzare i binding;
2. chiamare `Start Recording`;
3. osservare `On Observation Recorded`;
4. fermare/finalizzare il core;
5. salvare il `Timeline Bundle` ricevuto da `On Observation Track Finalized`.

Workflow replay:

1. chiamare `Prepare Replay` con l'Action Track del bundle;
2. in `Ready`, chiamare `Start Observation Comparison`;
3. chiamare `Start Replay`;
4. osservare i delegate Compared/Matched/Unexpected/Ambiguous;
5. leggere Journal e Summary quando `On Observation Journal Completed` viene emesso.

## Policy

Le query State/Event/Sense/Cause vengono configurate in
`FIntentReplayObservationRecordOptions`. Per default gli Event richiedono un Source Entity ID
valido. Le tolleranze e il comportamento dei tie sono in
`FIntentReplayObservationMatchOptions`.

`bTreatPersistentStateObservationsAsOrderedSnapshots` richiede
`bStrictPersistentIdentity`. È indicato quando movimento, orientamento o recovery possono
ritardare una riacquisizione Sight oltre `StateLateTolerance`: conserva gli snapshot State in
ordine e continua a confrontarne status e valore. Non rende equivalenti Entity ID o State Tag
diversi.

`bTreatVerifiedCausalEventsAsOccurrenceIdentity` è un opt-in per giochi nei quali una Source
riprodotta può essere temporaneamente interrotta o ricalcolare il proprio percorso. Abilitarlo
solo quando le richieste della Source passano da Intent Replay: il resolver richiede una
correlazione replay-owned verificata e continua a imporre identità persistente, semantica,
Cause/Instigator e consumo one-to-one. Non sostituisce l'assegnazione stabile degli Entity ID.
