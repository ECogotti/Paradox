# IntentReplay

## Interruzioni recuperabili

Un'autorità esterna può sospendere atomicamente il replay con
`BeginExternalReplayInterruption`. Gli intenti replay-owned vengono catturati come snapshot
immutabili e interrotti con un reason esplicito; il Journal conserva l'evento senza classificarlo
come fracture. Ogni intento deve essere rieseguito o risolto come già soddisfatto prima che
`ResumeReplay` venga accettato.

`IntentReplay` è un plugin runtime per Unreal Engine 5.8 che registra le richieste semantiche accettate da `GameplayActions`, le finalizza in track immutabili e le riproduce su un altro Actor.

Il caso principale è il ciclo:

```text
player registra -> track finalizzato -> reset del mondo -> clone riceve il track -> replay
```

La dipendenza resta unidirezionale:

```text
IntentReplay -> GameplayActions
IntentReplayPerception -> IntentReplay + PerceptionKnowledge
```

`IntentReplay` resta il modulo core generico e non dipende da AI, `PerceptionKnowledge`,
Behavior Tree, GoalAgents, GridWorld o codice specifico di Paradox. Il modulo runtime opzionale
`IntentReplayPerception` contiene esclusivamente l'adapter percettivo della Milestone 2.

## Durata completa della timeline

Il completamento del replay richiede contemporaneamente:

- tutte le entry inviate;
- tutte le Gameplay Action replay-owned terminali;
- tutte le interruzioni esterne riconciliate;
- il raggiungimento di `UIntentReplayTrack::RecordedDuration`.

L'ultimo requisito conserva anche una coda inattiva registrata dopo l'ultima action. Durante quel
tratto la sessione resta `Playing`, il clock continua ad avanzare e gli adapter sincronizzati, come
`IntentReplayPerception`, continuano il confronto. `PauseReplay` congela anche questa attesa finale;
`ResumeReplay` la ripianifica usando il tempo residuo, senza polling o wait action sintetiche.

## Setup

Ogni entità coinvolta deve possedere:

- un `UGameplayActionComponent`, autorità delle action locali;
- un `UIntentReplayComponent`, collegato al componente action dello stesso Actor.

Il binding avviene tramite `Action Component Override` oppure cercando il primo `UGameplayActionComponent` sull’owner. Un Action Component ammette un solo journal sink: se un altro sink è già registrato, l’inizializzazione fallisce in modo osservabile.

Le `UGameplayActionDefinition` che devono entrare nei track devono usare journaling `Required` o `Optional`. `Required` è la scelta consigliata perché il sink può rifiutare transazionalmente un `Accepted` non registrabile. Le Definition con journal `Disabled` restano osservabili nell’Execution Journal, ma non entrano nel track.

## Modello di ownership

- `UIntentRecordingSession` appartiene al componente del recorder.
- `UIntentReplayPlaybackSession` appartiene al componente del destinatario.
- `UIntentExecutionJournal` appartiene alla relativa recording/playback session, oppure al componente per il journal ambientale.
- `UIntentReplayTrack` finalizzato viene creato nel transient package, non sotto l’Actor sorgente.

Il track non contiene hard reference al player, al clone, alle action instance o agli handle runtime. Le Definition sono identificate tramite Primary Asset ID e soft reference.

Un track è comunque un UObject transient: il coordinatore che deve conservarlo durante il reset deve mantenerlo in una proprietà riflessa:

```cpp
UPROPERTY(Transient)
TObjectPtr<UIntentReplayTrack> PendingCloneTrack;
```

Una variabile C++ raw, una variabile locale o un puntatore non riflesso non proteggono il track dal Garbage Collector. Il proprietario della `UPROPERTY` deve sopravvivere al reset, per esempio un coordinatore posseduto dal `GameInstance`.

## Registrazione

Il componente espone:

- `StartRecording`;
- `PauseRecording` e `ResumeRecording`;
- `RequestStopRecording`;
- `CancelRecording`;
- `OnRecordingFinalized`.

Gli stati sono `Created`, `Recording`, `Paused`, `Draining`, `Finalized`, `Failed` e `Cancelled`.

Il sink copia in modo isolato Definition identity, Property Bag, priorità, blocked policy, lock, interruptibility, timeout, origin, correlation, sequence e timestamp relativo. Il risultato originale viene aggiunto soltanto se l’`Ended` arriva prima della finalizzazione.

`Immediate` è il default di `RequestStopRecording`: finalizza prima del ritorno e lascia
`bHasOriginalResult == false` per le action ancora attive. `AsyncStop` smette immediatamente di
accettare nuove entry, attende in modo event-driven gli `Ended` delle action già registrate e poi
emette `OnRecordingFinalized`, senza bloccare il Game Thread.

Il precedente nome `DrainTrackedActions` è nascosto e deprecato, ma resta riconosciuto per non
rompere Blueprint già serializzati. Nei nuovi grafi usare `AsyncStop` quando serve conservare i
risultati terminali originali.

Una nuova chiamata a `StartRecording` crea sempre un nuovo Track ID, un nuovo Recording Session ID,
un nuovo clock e un nuovo storage. Non svuota né riutilizza il track precedentemente trasferito.

Il formato core corrente è `2`: ogni `FRecordedIntent` conserva sia il `TrackSequence` locale sia il
`TimelineSequence` condiviso con eventuali canali sincronizzati. L'ordinamento è timestamp relativo,
timeline sequence, track sequence. I track legacy formato `1` restano validabili e riproducibili,
usando `TrackSequence` come fallback; un bundle percettivo richiede invece il formato `2`.

`GetRecordingClockSnapshot`, `GetPlaybackClockSnapshot`,
`CaptureRecordingTimelinePoint` e `CapturePlaybackTimelinePoint` espongono snapshot per valore e
un'allocazione atomica della sequence. I clock sono congelati durante la pausa e dopo gli stati
terminali. `OnTimelineLifecycleChanged` e il corrispondente delegate native notificano una sola volta
ogni transizione autorevole.

## Playback

`PrepareReplay` valida il track, risolve asincronamente le Definition mancanti e crea richieste nuove sul componente del destinatario. Il risultato può essere:

- `Ready`: è possibile chiamare subito `StartReplay`;
- `Preparing`: attendere `OnReplayPrepared`;
- `Rejected`: consultare il failure strutturato.

Il replay usa timer one-shot e timestamp assoluti relativi alla sessione, senza Tick permanente. Ogni entry produce una nuova request, una nuova action instance e un nuovo handle locale.

Le request di replay usano:

- origin `GameplayAction.Origin.Replay`;
- correlation type `IntentReplay.Correlation.RecordedIntent`;
- correlation GUID uguale al `RecordedIntentId`.

Il default ferma la sessione se il submit viene rifiutato. Un fallimento terminale di una action viene invece registrato nel journal e la timeline continua, salvo configurazione diversa.

Anche con la terminal failure policy `StopPlayback`, una action interrotta da una successiva action
della stessa playback session rappresenta la riproduzione intenzionale del pattern registrato e non
ferma la timeline. Una preemption causata da un'action esterna alla sessione resta invece un failure.

Il replay è `Completed` soltanto quando tutte le entry sono state processate e tutte le action create dalla sessione sono terminali. `StopReplay` e i failure cancellano esclusivamente gli handle della sessione.

## Track condivisi

Lo stesso `UIntentReplayTrack` può essere passato a più componenti e riprodotto contemporaneamente. Il track è immutabile; timer, indice corrente, handle, risultati e journal appartengono alle singole playback session.

## Documentazione correlata

- [Workflow player → clone](PlayerCloneWorkflow.md)
- [API Blueprint e C++](CppAPI.md)
- [Compatibilità, failure e troubleshooting](Debugging.md)
- [Modulo percettivo: setup e workflow](../Source/IntentReplayPerception/Docs/README.md)
- [Report: replay di BP_TopDownCharacter](Reports/BP_TopDownCharacterReplayInvestigation.md)
