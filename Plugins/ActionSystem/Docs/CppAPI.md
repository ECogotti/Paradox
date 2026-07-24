# API e ownership C++

## Observer nativo Ended

`UGameplayActionComponent::OnActionEndedNative()` permette ai plugin ponte C++ di conservare un
`FDelegateHandle` esatto. Viene trasmesso dopo i delegate Blueprint generico e `OnActionEnded`,
mentre l'istanza transient è ancora posseduta, e immediatamente prima del suo rilascio. Gli observer
devono conservare handle e result dell'evento, non il puntatore all'istanza.

`GameplayAction.Lock.Movement` è il lock exact-match condiviso dalle action che guidano in modo
esclusivo il movimento di Pawn o Character.

## Dipendenza e include

Nel `Build.cs` del consumer aggiungere:

```csharp
PrivateDependencyModuleNames.Add("GameplayActions");
```

Include principali:

```cpp
#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Interfaces/GameplayActionJournalSink.h"
```

## Creazione e submit

La stessa factory usata da Blueprint è autoritativa anche in C++:

```cpp
FGameplayActionRequestCreationResult Creation =
    UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);

if (!Creation.WasCreated())
{
    return;
}

UGameplayActionBlueprintLibrary::SetRequestPriority(Creation.Request, 25);

FGameplayActionCorrelationData Correlation;
Correlation.Type = CorrelationTypeTag;
Correlation.Id = FGuid::NewGuid();
UGameplayActionBlueprintLibrary::SetRequestContext(
    Creation.Request, OriginTag, Requester, Correlation);

const FGameplayActionSubmissionResult Submission =
    ActionComponent->SubmitAction(Creation.Request);
```

Per integrazioni native basate su proprietà riflesse, `SetRequestParameterFromProperty` e `GetRequestParameterToProperty` sono le controparti C++ dei nodi wildcard. Applicano le stesse regole: tipo esatto, campo esistente, nessuna mutazione dello schema.

## Ownership

- La Definition è un asset posseduto dall'asset system.
- La request è un value type e possiede una copia del proprio Property Bag.
- Il componente crea ogni istanza con `NewObject` e la mantiene tramite una proprietà riflessa.
- L'istanza non deve essere conservata oltre il lifecycle event `Ended`.
- L'handle è il riferimento stabile per query e cancel.
- Il componente conserva solo `EGameplayActionState` e `FGameplayActionResult` dopo il rilascio dell'istanza.
- Eventi e parametri evento sono copie indipendenti.

## Estendere un'azione

Derivare da `UGameplayActionInstance` e specializzare soltanto gli hook necessari:

- `CanStartAction`;
- `OnActionInit`;
- `OnActionStarted`;
- `OnActionTick` (opt-in);
- `OnActionPaused` / `OnActionResumed`;
- `OnActionCancelled`;
- `OnActionInterrupted`;
- `OnActionAborted`;
- `OnActionCleanup`.

`OnActionInit` viene chiamato una sola volta per ogni istanza accettata, inclusa un'istanza queued. Usarlo per leggere gli snapshot e predisporre stato locale. `OnActionStarted` viene chiamato una sola volta dopo l'acquisizione dei lock e deve contenere il lavoro che richiede l'esecuzione reale.

Terminare il lavoro esclusivamente con `SucceedAction` o `FailAction`. Non cambiare direttamente lo stato. Timer, delegate e risorse asincrone devono essere sempre annullati in `OnActionCleanup`; il componente ignora callback terminali tardive.

`UGameplayWaitAction` è l'implementazione di riferimento: valida e memorizza `Duration` in Init, crea il timer soltanto in Start, mette in pausa/riprende il timer e lo rimuove in cleanup.

## Tick opt-in

Il tick è disabilitato per default. Una classe nativa può abilitarlo nel costruttore e implementare l'hook protetto:

```cpp
UMyGameplayAction::UMyGameplayAction()
{
    bActionTickEnabled = false;
}

void UMyGameplayAction::OnActionInit_Implementation()
{
    // Leggere parametri e memorizzare riferimenti. Nessun lavoro che richieda i lock.
}

void UMyGameplayAction::OnActionStarted_Implementation()
{
    SetActionTickEnabled(true);
}

void UMyGameplayAction::OnActionTick_Implementation(const float DeltaSeconds)
{
    // Aggiornamento per-frame sul Game Thread, solo mentre lo stato è Running.
}
```

Una sottoclasse può chiamare `SetActionTickEnabled` per abilitarlo o disabilitarlo dinamicamente. Il componente usa uno snapshot deterministico degli handle attivi, sospende il dispatch durante `PauseActions` e disabilita il proprio tick quando nessuna azione running è opt-in. Le azioni avviate durante un tick vengono elaborate dal frame successivo.

## Queue timeout

La Definition espone `MaxQueueTimeSeconds`. Il valore viene copiato nell'istanza accettata e non cambia se l'asset viene modificato in seguito. I getter dell'istanza sono:

- `GetMaxQueueTimeSeconds()`;
- `GetQueueElapsedSeconds()`;
- `GetQueueRemainingSeconds()`;
- `HasQueueTimeout()`;
- `IsQueueTimeUnlimited()`.

Il componente aggiorna il tempo in ordine scheduler prima di distribuire gli Action Tick. La scadenza usa il normale percorso terminale `Failed`, con `GameplayAction.Result.Failure.QueueTimeout`, Cleanup, evento Ended e rilascio dell'istanza.

## Journal sink

Un sink nativo implementa `IGameplayActionJournalSink::WriteGameplayActionEvent_Implementation` e viene registrato con `RegisterJournalSink`. Il componente mantiene un solo sink per volta. La chiamata è sincrona e avviene sul Game Thread. Il sink non deve conservare riferimenti agli oggetti transient contenuti nell'evento né effettuare operazioni rientranti durante la transazione Accepted.
