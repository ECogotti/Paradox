# Workflow player → reset → clone

L’architettura supporta il trasferimento della registrazione fra entità diverse. Il reset non deve spostare la recording session del player: trasferisce soltanto il track finalizzato.

## Sequenza consigliata

1. Il coordinatore del viaggio nel tempo blocca nuove richieste provenienti dall’input del player.
2. Cancella o porta a termine le action ancora attive sul `UGameplayActionComponent` del player.
3. Richiede `AsyncStop` e attende `OnRecordingFinalized`, senza attese sincrone sul Game Thread.
4. Salva il track ricevuto in una `UPROPERTY(Transient)` posseduta da un oggetto che sopravvive al reset.
5. Ripristina il mondo e spawna il clone.
6. Passa il track al `UIntentReplayComponent` del clone.
7. Chiama `PrepareReplay`; se il risultato è `Preparing`, attende `OnReplayPrepared`, poi chiama `StartReplay`.
8. Avvia una nuova recording session sul player ripristinato.

Se è attivo `IntentReplayPerception`, il coordinatore conserva invece il
`UIntentReplayTimelineBundle` emesso da `OnObservationTrackFinalized`. Il bundle trattiene sia
l'Action Track sia l'Observation Track e ne impedisce il pairing tra sessioni diverse. Dopo il reset:

1. passa `Bundle->GetActionTrack()` a `PrepareReplay`;
2. quando il core è `Ready`, chiama `StartObservationComparison(Bundle, Options)`;
3. chiama `StartReplay`;
4. conserva il bundle in una `UPROPERTY(Transient)` per tutto il replay.

Non ricostruire il bundle da due puntatori raw e non armare la comparison dopo che il clock è
avanzato, salvo opt-in esplicito al late join.

Il default di `RequestStopRecording` è `Immediate`: il track è disponibile nello stesso ramo di
esecuzione, ma le entry ancora in esecuzione non avranno il risultato originale. Usare esplicitamente
`AsyncStop` soltanto quando il coordinatore può proseguire da `OnRecordingFinalized` e necessita dei
risultati terminali originali.

`GetLastFinalizedTrack` non rappresenta la sessione mutable corrente. Prima della prima
finalizzazione restituisce `None`; nelle registrazioni successive continua invece a indicare il track
precedente finché quello nuovo non viene pubblicato. Chiamare `GetEntryCount` su un riferimento
`None` produce `Accessed None` e un apparente zero; usare il riferimento precedente riprodurrebbe
invece l'iterazione sbagliata. Con `AsyncStop`, il clone deve essere spawnato o preparato dal ramo
`OnRecordingFinalized`. Con `Immediate`, il getter è valido al ritorno di una stop request riuscita.

## Coordinatore C++

```cpp
UPROPERTY(Transient)
TObjectPtr<UIntentReplayTrack> PendingCloneTrack;

void UTimeResetCoordinator::HandlePlayerTrackFinalized(UIntentReplayTrack* Track)
{
    PendingCloneTrack = Track;
    RestoreWorldAndSpawnClone();
}

void UTimeResetCoordinator::AssignTrackToClone(UIntentReplayComponent* CloneReplay)
{
    FIntentReplayPlaybackOptions Options;
    const FIntentReplayPrepareResult Result =
        CloneReplay->PrepareReplay(PendingCloneTrack, Options);

    if (Result.Status == EIntentReplayPrepareStatus::Ready)
    {
        CloneReplay->StartReplay();
    }
    // Per Preparing, il coordinatore attende OnReplayPrepared.
}
```

Il coordinatore deve gestire esplicitamente i failure di finalizzazione e preparazione. Non deve distruggere il player prima di aver conservato il track.

## Blueprint

Sul player:

1. aggiungere `Gameplay Action Component`;
2. aggiungere `Intent Replay Component`;
3. chiamare `Start Recording`;
4. al reset, interrompere la produzione di input/action;
5. terminare le action attive;
6. chiamare `Request Stop Recording` lasciando `Immediate` per un flusso sincrono, oppure selezionare
   `Async Stop` se si devono attendere i risultati delle action tracciate;
7. con `Async Stop`, salvare il riferimento sul coordinatore persistente nell’evento
   `On Recording Finalized`; con `Immediate` è anche valido leggere `Last Finalized Track` subito
   dopo una richiesta riuscita.

Sul clone:

1. assegnare il track a `Prepare Replay`;
2. se `Ready`, chiamare `Start Replay`;
3. se `Preparing`, usare `On Replay Prepared`;
4. osservare `On Replay Completed`, `On Replay Failed` e `On Replay Stopped`.

## Behavior Tree

Il Behavior Tree può orchestrare il componente da codice esterno al plugin:

- un BT Task chiama `PrepareReplay`;
- attende `Ready` o `OnReplayPrepared`;
- chiama `StartReplay`;
- conclude quando riceve un evento terminale o legge uno stato terminale.

Questa integrazione appartiene al modulo AI del gioco. `IntentReplay` non include `AIModule` e non conosce Behavior Tree, evitando una dipendenza architetturale dal sistema AI scelto.

## Più cloni

Per riprodurre la stessa storia su più entità, conservare una sola `UPROPERTY` al track e chiamare `PrepareReplay` su ogni clone. Non duplicare o modificare il track. Ogni componente crea una playback session con GUID, journal, timeline e handle propri.
