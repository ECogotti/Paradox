# API Blueprint e C++

## Interruzione esterna recuperabile

`BeginExternalReplayInterruption(ReasonTag)` è valido soltanto in `Playing`. Mette in pausa clock
ed emissioni, cattura `FIntentReplaySuspendedIntent`, registra il reason atteso e interrompe gli
handle replay-owned. Le interruzioni restano nel Journal e non sono replay fracture.

`ResumeReplay` restituisce `PendingExternalRecovery` finché ogni Recorded Intent non viene
riconciliato con `ReissueExternallyInterruptedIntent` oppure
`ResolveExternallyInterruptedIntentAsSatisfied`. Il reissue usa la request già preparata; la
risoluzione satisfied aggiunge un record diagnostico sintetico. Nessuna API modifica il track.

## Tipi principali

- `UIntentReplayComponent`: binding, recording, preparazione e playback.
- `UIntentRecordingSession`: stato mutable della registrazione e relativo journal.
- `UIntentReplayTrack`: snapshot finalizzato, immutabile e condivisibile.
- `UIntentReplayPlaybackSession`: stato runtime del replay sul destinatario.
- `UIntentExecutionJournal`: eventi osservati, inclusi divergence e failure.

Track, recorded intent e playback session espongono ID GUID Blueprint-visible. Gli array interni e il Property Bag registrato non sono Blueprint-writable.

Ogni recording attempt espone anche un `FIntentRecordingSessionId`, distinto dal Track ID. I track
formato `2` pubblicano questo ID e ogni entry espone il `TimelineSequence` condiviso.

## Esempio di registrazione C++

```cpp
FIntentRecordingOptions Options;
Options.SourceLabel = TEXT("Player.Iteration.3");
Options.MaxTrackEntries = 0; // illimitato

const FIntentRecordingStartResult StartResult =
    IntentReplayComponent->StartRecording(Options);

if (!StartResult.Succeeded())
{
    // Usare Status e Failure, senza interpretare DiagnosticMessage come dato gameplay.
}

const FIntentReplayTimelinePointResult ExternalPoint =
    IntentReplayComponent->CaptureRecordingTimelinePoint(StartResult.SessionId);

if (ExternalPoint.Succeeded())
{
    // RelativeTimeSeconds e TimelineSequence sono stati acquisiti atomicamente.
}
```

Per terminare:

```cpp
// Default: finalizzazione sincrona Immediate.
IntentReplayComponent->RequestStopRecording();

// Variante asincrona: attendere OnRecordingFinalized.
IntentReplayComponent->RequestStopRecording(
    EIntentRecordingFinalizeMode::AsyncStop);
```

Con il default `Immediate`, il track è leggibile con `GetLastFinalizedTrack` al ritorno di una
richiesta riuscita. `AsyncStop` smette subito di accettare nuove entry ma pubblica il track soltanto
dopo gli `Ended` delle action già tracciate: in quel caso usare `OnRecordingFinalized`. La sessione
conclusa e il suo journal sono disponibili tramite `GetLastRecordingSession`.

`DrainTrackedActions` è il nome legacy, nascosto e deprecato, mantenuto solo per caricare Blueprint
serializzati da versioni precedenti. Nuovo codice e nuovi nodi devono usare `AsyncStop`.

## Esempio di replay C++

```cpp
FIntentReplayPlaybackOptions Options;
Options.CompatibilityPolicy =
    EIntentReplayCompatibilityPolicy::StrictRecordedSchema;

const FIntentReplayPrepareResult PrepareResult =
    CloneReplayComponent->PrepareReplay(Track, Options);

if (PrepareResult.Status == EIntentReplayPrepareStatus::Ready)
{
    CloneReplayComponent->StartReplay();
}
```

La sessione espone in sola lettura:

- stato e Session ID;
- track sorgente;
- prossimo indice;
- numero e snapshot degli handle posseduti;
- Execution Journal;
- report di compatibilità per entry.

## Clock e lifecycle generici

Le integrazioni runtime opzionali devono usare il clock core, senza crearne uno parallelo:

```cpp
const FIntentReplayTimelineClockSnapshot Clock =
    IntentReplayComponent->GetPlaybackClockSnapshot();

const FIntentReplayTimelinePointResult Point =
    IntentReplayComponent->CapturePlaybackTimelinePoint(Clock.PlaybackSessionId);
```

Una capture può restituire `Paused`, `NotAccepting`, `SessionMismatch`, `NoActiveSession`,
`WrongThread` o `ShuttingDown`. La sequence viene incrementata soltanto per una capture riuscita.

`OnTimelineLifecycleChangedNative()` è il canale C++ per adapter sincronizzati;
`OnTimelineLifecycleChanged` è l'equivalente Blueprint. Gli eventi pubblici preesistenti mantengono
firme e comportamento.

## Parametri registrati

`FRecordedIntent` espone metadati e configurazione in sola lettura. In Blueprint usare `Get Recorded Intent Parameter`, un nodo wildcard type-safe che non modifica schema o valore.

In C++:

```cpp
FRecordedIntent Entry;
if (Track->GetEntryByIndex(0, Entry))
{
    const TValueOrError<double, EPropertyBagResult> Duration =
        Entry.GetParameters().GetValueDouble(TEXT("Duration"));
}
```

## Recordability

La policy predefinita valida ricorsivamente struct, array, set e mappe.

Sono ammessi:

- valori primitivi e struct deterministiche;
- classi e soft reference;
- hard reference ad asset stabili.

Sono rifiutati con path e motivo strutturati:

- Actor e Actor Component;
- UObject transient;
- UObject non asset;
- tipi riflessi non supportati.

La policy è sostituibile in C++ tramite `RecordabilityPolicyClass`. La strategia di submit è sostituibile tramite `ExecutionStrategyClass`; il default inoltra la request preparata al `UGameplayActionComponent` del destinatario.

## Regole di utilizzo

- Non cambiare Action Component durante una sessione attiva.
- Non chiamare API di submit/cancel dall’interno della transazione journal sincrona.
- Non conservare action instance oltre `Ended`.
- Conservare i track destinati al reset tramite `UPROPERTY`.
- Attendere `Ready` prima di `StartReplay`.
