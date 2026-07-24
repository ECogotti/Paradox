# Modulo GameplayActions

Il modulo runtime `GameplayActions` implementa tipi pubblici, Definition, lifecycle separato Init/Start, istanze con tick opt-in, queue timeout, scheduler, journal, wait action, diagnostica e automation test del plugin.

La documentazione completa per utenti e integratori è nel [Docs del plugin](../../../Docs/README.md). In particolare:

- [API e ownership C++](../../../Docs/CppAPI.md)
- [Scheduler](../../../Docs/Scheduler.md)
- [Debug](../../../Docs/Debugging.md)

Il modulo espone solo dipendenze runtime (`Core`, `CoreUObject`, `Engine`, `GameplayTags`) e non deve dipendere da moduli Editor o da plugin di gioco.

## Punti di integrazione

- `GameplayAction.Lock.Movement` e il lock esatto condiviso dalle action che controllano il movimento.
- `UGameplayActionComponent::OnActionEndedNative()` permette ai bridge C++ di osservare `Ended`
  dopo i delegate Blueprint ma prima che l'istanza venga rimossa dal componente.
- I bridge opzionali `GameplayActionsAI` e `GameplayActionsGridWorld` dipendono da questo modulo;
  il core non dipende da AI, StateTree o GridWorld.
