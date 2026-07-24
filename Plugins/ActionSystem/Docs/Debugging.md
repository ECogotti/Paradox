# Debug e troubleshooting

## Logging

Il modulo usa una sola categoria, `LogGameplayActions`, e le macro:

- `GAMEPLAYACTIONS_LOG_INFO`;
- `GAMEPLAYACTIONS_LOG_WARNING`;
- `GAMEPLAYACTIONS_LOG_ERROR`.

I dettagli dello scheduler richiedono entrambe le condizioni:

```text
GameplayActions.Debug = 1
bEnableDebug = true sul componente interessato
```

La CVar globale disabilita immediatamente i dettagli di tutti i componenti. Il tick runtime delle action è opt-in e non produce debug drawing spaziale.

## Snapshot

`Get Debug Snapshot` restituisce una copia read-only con:

- owner, stato pausa e accettazione submission;
- azioni active e queued;
- handle, Action Tag, stato, priorità, lock, sequence ed elapsed time;
- limite, tempo trascorso e secondi residui della queue;
- flag espliciti per timeout attivo o queue illimitata;
- ultimo risultato terminale;
- ultima decisione dello scheduler.

## Unreal Insights

Sono presenti scope CPU per:

- `GameplayActions_Submit`;
- `GameplayActions_Preflight`;
- `GameplayActions_ActionTick`;
- `GameplayActions_QueueEvaluation`;
- `GameplayActions_Finish`;
- `GameplayActions_EventDispatch`.

## Problemi comuni

`RejectedInvalidRequest` indica normalmente una request non creata dalla factory, Definition cambiata dopo la factory, schema diverso, classe astratta/mancante, Action Tag mancante o lock fuori dalla gerarchia.

`RejectedValidation` proviene da `CanStartAction`; controllare Reason Tag e diagnostic message.

`RejectedBlocked` indica conflitto con policy `Reject`, parità/inferiorità di priorità o conflitto non interruptible.

`RejectedJournal` indica che una Definition `Required` non ha sink oppure che il sink ha rifiutato l'evento Accepted.

`RejectedReentrant` indica una chiamata effettuata durante validazione o transazione iniziale del journal. Spostare l'operazione su un lifecycle delegate o differirla.

`GameplayAction.Result.Failure.QueueTimeout` indica che una action accettata non ha acquisito i lock entro `Max Queue Time Seconds`. Nell'evento Ended e nello snapshot verificare `MaxQueueTimeSeconds`, `QueueElapsedSeconds` e `QueueRemainingSeconds`.

Se un'azione resta in coda, verificare prima che l'azione che sembra conclusa abbia davvero chiamato `Succeed Action` o `Fail Action`: terminare il grafo Blueprint di `Action Start` non cambia lo stato `Running`. Controllare poi `bPaused`, gli exact lock nelle entry active/queued e `LastSchedulerDecision`; quando la coda resta bloccata, la decisione riporta il primo handle queued e gli handle attivi in conflitto.

Se `Action Tick` non viene chiamato, verificare `Action Tick Enabled`, lo stato `Running` e che il componente non sia in pausa. Il component tick può essere attivo soltanto per aggiornare queue timeout: ciò non implica che una action queued riceva `Action Tick`.

## Avviso “compiled with a different engine version”

Il popup di Unreal per un modulo plugin mancante, stale o incompatibile usa un testo generico. Per questo progetto la verifica corretta è:

1. chiudere l'Editor;
2. compilare il target completo `ParadoxEditor Win64 Development` con UE 5.8, `-WaitMutex` e `-NoHotReloadFromIDE`;
3. verificare che esista un solo `UnrealEditor-GameplayActions.dll`;
4. verificare che il `.modules` del plugin e il receipt del target riportino il BuildId `55116800`;
5. avviare l'Editor due volte e cercare `Incompatible or missing module: GameplayActions` nei log.

Non aggiungere un `EngineVersion` artificiale a `GameplayActions.uplugin`: non corregge DLL mancanti o output Hot Reload. Le occorrenze osservate durante lo sviluppo erano associate a un modulo non ancora compilato o stale; dopo il target build completo il modulo viene caricato normalmente.

Per eseguire la suite:

```powershell
UnrealEditor-Cmd.exe Paradox.uproject -unattended -nop4 -NullRHI `
  '-ExecCmds=Automation RunTests GameplayActions.' `
  '-TestExit=Automation Test Queue Empty'
```
