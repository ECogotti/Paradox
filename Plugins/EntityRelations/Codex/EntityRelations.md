# EntityRelations — Architettura del Plugin UE5

## Scopo del documento

Questo documento definisce l'architettura e i requisiti di implementazione del plugin runtime `EntityRelations` per Unreal Engine 5.

Il plugin deve fornire un sistema generico, direzionale, modulare e data-driven per:

- identificare entità runtime;
- descrivere relazioni tra una entità sorgente e una entità bersaglio;
- conservare eventuale stato esplicito e asimmetrico tra due entità;
- valutare relazioni attraverso policy sostituibili;
- classificare il risultato della relazione;
- suggerire possibili conseguenze semantiche senza applicarle;
- permettere query C++ e Blueprint;
- supportare query frequenti provenienti da AI, percezione e strumenti tattici;
- fornire diagnostica, logging, profiling e strumenti di debug dedicati.

Il plugin deve essere riutilizzabile in altri progetti e non deve contenere regole hardcoded specifiche di **Paradox**.

La regola di Paradox secondo cui una versione temporale con indice minore genera un paradosso quando percepisce una versione con indice maggiore deve essere implementata nel modulo di gioco tramite policy specifiche che utilizzano il plugin.

---

# 1. Istruzioni obbligatorie per Codex

Prima di creare o modificare codice:

1. Leggere il file root `AGENTS.md`.
2. Individuare il plugin o modulo che possiede il sistema.
3. Cercare e leggere tutti i file Markdown nei `CODEX` rilevanti.
4. Leggere la documentazione esistente nei relativi folder `Docs`.
5. Ispezionare le convenzioni già presenti nel repository per:
   - plugin runtime;
   - subsystem;
   - Actor Component;
   - Gameplay Tags;
   - Data Asset;
   - logging;
   - Developer Settings;
   - test automatici;
   - debug runtime.
6. Verificare ogni API Unreal utilizzata contro gli header disponibili per la versione UE del progetto.
7. Implementare la soluzione minima che rispetta questa architettura.
8. Compilare il target appropriato dopo ogni modifica significativa.
9. Aggiornare la documentazione del plugin nel folder `Docs`.
10. Non considerare il lavoro completato finché il codice interessato non compila.

Non modificare codice generato o cartelle transient come `Binaries`, `Intermediate`, `Saved` o `DerivedDataCache`.

---

# 2. Principi architetturali

## 2.1 Le relazioni sono direzionali

La relazione deve essere sempre valutata nella forma:

```text
Source -> Target
```

Il risultato di:

```text
A -> B
```

può essere diverso da:

```text
B -> A
```

Non normalizzare mai automaticamente la coppia e non assumere simmetria.

Questa proprietà è fondamentale per sistemi come:

- conoscenza;
- fiducia;
- reputazione;
- percezione;
- gerarchie;
- relazioni temporali;
- permessi di interazione;
- regole di combattimento.

---

## 2.2 Identità, stato diretto, policy e risultato sono concetti separati

Il plugin deve distinguere chiaramente:

1. **identità dell'entità**;
2. **stato esplicito Source -> Target**;
3. **policy di valutazione**;
4. **risultato immutabile della query**.

Non concentrare questi concetti in una singola struttura o in un enum generico come `Ally`, `Enemy`, `Neutral`.

---

## 2.3 Le query non producono effetti di gameplay

La valutazione di una relazione deve essere priva di effetti collaterali.

Una query può:

- classificare una relazione;
- restituire `Allow`, `Deny` o `NoOpinion`;
- restituire tag di risultato;
- suggerire possibili outcome;
- fornire motivazioni diagnostiche.

Una query non deve:

- applicare danni;
- generare un paradosso;
- cambiare fazione;
- modificare lo stato delle entità;
- emettere rumori;
- attivare Gameplay Actions;
- cambiare il mondo;
- notificare automaticamente sistemi esterni come conseguenza della valutazione.

Il sistema chiamante decide se e come applicare il risultato.

Questo vincolo permette di usare le stesse query anche per:

- preview tattiche;
- UI;
- validazione preventiva;
- AI planning;
- debug;
- simulazione;
- test automatici.

---

## 2.4 Il core è generico

Il plugin non deve conoscere direttamente:

- `Temporal Index`;
- cloni;
- rewind;
- paradossi;
- puzzle;
- player;
- fazioni concrete del gioco;
- sistemi di combattimento concreti;
- coni visivi;
- propagazione del rumore;
- Goal specifici;
- regole specifiche di una mappa.

Le regole di progetto devono essere implementate in moduli che dipendono da `EntityRelations`, non dentro il plugin generico.

---

## 2.5 Stato esplicito sparso

Non creare una matrice completa `N x N` tra tutte le entità registrate.

Lo stato diretto deve essere memorizzato solo quando una coppia possiede davvero informazioni esplicite.

Esempi:

```text
A conosce B
A si fida di B
A ha identificato B
A possiede un override verso B
A considera B temporaneamente ostile
```

Le relazioni completamente derivabili da identità, componenti e policy non devono essere duplicate come stato persistente.

---

## 2.6 Un solo proprietario autorevole dello stato

L'identità è posseduta dal componente identità dell'entità.

Lo stato diretto `Source -> Target` è posseduto dal componente stato della sorgente.

Il registry e la cache runtime sono posseduti dal World Subsystem.

Le policy configurano il calcolo ma non devono conservare stato mutabile per singola query.

---

# 3. Confini del plugin

## 3.1 Responsabilità interne

`EntityRelations` deve gestire:

- identificatori logici delle entità;
- registrazione e deregistrazione runtime;
- ricerca di entità tramite ID;
- query direzionali;
- domini di relazione;
- policy e Policy Set;
- stato diretto opzionale `Source -> Target`;
- revisioni e invalidazione;
- cache opzionale delle query;
- query batch;
- eventi di registrazione e invalidazione;
- API Blueprint e C++;
- logging;
- profiling;
- debug e spiegazione delle query;
- test automatici;
- documentazione utente.

## 3.2 Responsabilità esterne

Il plugin non deve gestire:

- creazione o distruzione dei cloni;
- assegnazione delle generazioni temporali specifiche di Paradox;
- rewind;
- reset del mondo;
- percezione spaziale;
- trace e line of sight;
- tempo minimo di rilevamento;
- propagazione sonora;
- creazione effettiva di conseguenze;
- Gameplay Actions;
- replanning dei Goal;
- colori o widget della UI tattica;
- salvataggio globale del mondo;
- replica multiplayer nella prima implementazione, salvo requisiti già esistenti nel repository.

---

# 4. Struttura suggerita del plugin

Per una nuova implementazione usare, salvo convenzioni più specifiche già presenti nel repository:

```text
Plugins/
└── EntityRelations/
    ├── EntityRelations.uplugin
    ├── CODEX/
    │   └── ARCHITECTURE.md
    ├── Docs/
    │   └── README.md
    └── Source/
        └── EntityRelations/
            ├── EntityRelations.Build.cs
            ├── Public/
            │   ├── Components/
            │   ├── Data/
            │   ├── Policies/
            │   ├── Settings/
            │   ├── Subsystems/
            │   ├── Types/
            │   └── EntityRelations.h
            ├── Private/
            │   ├── Components/
            │   ├── Data/
            │   ├── Policies/
            │   ├── Settings/
            │   ├── Subsystems/
            │   ├── Tests/
            │   ├── Types/
            │   └── EntityRelations.cpp
            ├── CODEX/
            └── Docs/
```

Non creare un modulo Editor vuoto in previsione di funzionalità future.

Creare `EntityRelationsEditor` solamente quando esistono reali strumenti editor-only, per esempio:

- details customization;
- visualizzazione avanzata di Policy Set;
- validatori di asset;
- editor grafico delle relazioni;
- asset factory custom.

Il modulo runtime non deve dipendere da moduli editor.

---

# 5. Dipendenze del modulo

Il modulo runtime dovrebbe dipendere solo da ciò che utilizza realmente.

Dipendenze previste:

- `Core`;
- `CoreUObject`;
- `Engine`;
- `GameplayTags`;
- `DeveloperSettings`, solo se viene usato un `UDeveloperSettings` dedicato.

Mantenere private le dipendenze che non devono transitare attraverso gli header pubblici.

Non aggiungere dipendenze verso:

- `GridWorld`;
- `GameplayActions`;
- `WorldState`;
- `GoalAgents`;
- `IntentReplay`;
- moduli specifici di Paradox.

---

# 6. Tipi fondamentali

## 6.1 `FEntityRelationId`

Creare una `USTRUCT` Blueprint-compatible che rappresenti un identificatore logico opaco.

Scelta raccomandata:

- valore interno basato su `FGuid`;
- operazioni di validità;
- uguaglianza;
- hashing;
- conversione diagnostica in stringa;
- nessun riferimento diretto a UObject o Actor.

L'ID logico deve poter sopravvivere alla sostituzione dell'istanza runtime.

Non usare come identità autorevole:

- nome dell'Actor;
- path UObject;
- pointer runtime;
- indice in un array;
- nome generato automaticamente da Unreal.

Può essere aggiunto un `DebugName` separato e non autorevole per rendere più leggibili log e strumenti.

Distinguere chiaramente:

- ID esplicito e stabile, assegnato dal sistema chiamante;
- ID generato a runtime per entità effimere.

Un ID generato a runtime non deve essere presentato come stabile attraverso reset o ricostruzioni dell'Actor.

---

## 6.2 `EEntityRelationQueryStatus`

Creare un enum per distinguere la validità tecnica della query dalla decisione semantica.

Valori minimi suggeriti:

```text
Success
InvalidSource
InvalidTarget
SourceNotRegistered
TargetNotRegistered
MissingPolicySet
UnsupportedDomain
EvaluationFailed
```

Non convertire errori tecnici in un falso `Allow` o `Deny`.

---

## 6.3 `EEntityRelationDecision`

Usare una decisione ternaria:

```text
NoOpinion
Allow
Deny
```

`NoOpinion` significa che la policy o il Policy Set non ha espresso una decisione definitiva.

Una query tecnicamente valida può terminare con `NoOpinion`.

Non interpretare automaticamente `NoOpinion` come `Allow`.

L'eventuale comportamento di default deve essere deciso dal sistema chiamante oppure da una policy di default esplicita.

---

## 6.4 `FEntityRelationQueryContext`

Creare una struttura di contesto leggera e deterministica.

Contenuto minimo:

```text
Domain                 Gameplay Tag singolo obbligatorio
ContextTags            Gameplay Tag Container opzionale
NumericContext         mappa opzionale GameplayTag -> float
bRequestExplanation    abilita dati diagnostici dettagliati
bAllowCache             permette al chiamante di disabilitare la cache
```

Esempi di domini:

```text
Relation.Domain.General
Relation.Domain.Interaction
Relation.Domain.Damage
Relation.Domain.Blocking
Relation.Domain.VisualPerception
Relation.Domain.AudioPerception
Relation.Domain.GoalValidation
Relation.Domain.TacticalPreview
```

Il dominio descrive la domanda posta al sistema.

La stessa coppia di entità può avere risultati differenti in domini differenti.

Non aggiungere un generico `UObject* ContextObject` nella prima versione, perché renderebbe più complessi:

- lifetime;
- hashing;
- cache;
- serializzazione;
- determinismo;
- Blueprint usage.

Aggiungerlo in futuro solo in presenza di un caso d'uso reale e con un contratto chiaro.

---

## 6.5 `FEntityRelationQuery`

La query deve contenere almeno:

```text
SourceId
TargetId
Context
```

Le API possono fornire overload o wrapper che accettano:

- ID logici;
- `UEntityIdentityComponent*`;
- `AActor*`, risolvendo il relativo Identity Component.

La forma interna autorevole deve sempre risolvere la coppia in entità registrate.

Non conservare la query come stato persistente nel subsystem.

---

## 6.6 `FEntityRelationReason`

Ogni motivazione deve essere strutturata e identificabile.

Contenuto suggerito:

```text
PolicyId
ReasonTag
OptionalDebugMessage
```

Il testo descrittivo deve essere costruito solamente quando:

- `bRequestExplanation` è attivo;
- il debug globale è attivo;
- uno strumento diagnostico lo richiede.

Usare tag o ID stabili come dato autorevole, non confronti su stringhe di debug.

---

## 6.7 `FEntityRelationResult`

Il risultato deve essere una fotografia del calcolo.

Contenuto minimo:

```text
Status
Decision
ClassificationTags
OutcomeTags
Reasons
WinningPolicyId
bWasCacheHit
```

### Classification Tags

Descrivono che cosa rappresenta la relazione.

Esempi:

```text
Relation.Classification.Temporal.Past
Relation.Classification.Temporal.Future
Relation.Classification.Temporal.SameGeneration
Relation.Classification.Affiliation.Ally
Relation.Classification.Affiliation.Enemy
Relation.Classification.Knowledge.Known
```

### Outcome Tags

Descrivono possibili conseguenze semantiche che il chiamante può decidere di applicare.

Esempi:

```text
Relation.Outcome.ParadoxCandidate
Relation.Outcome.ValidDistraction
Relation.Outcome.InteractionForbidden
Relation.Outcome.DamageAllowed
```

Il plugin non applica gli outcome.

Il risultato non deve esporre riferimenti mutabili agli array o alle mappe interne del subsystem.

---

# 7. Componente identità

## 7.1 `UEntityIdentityComponent`

Creare un Actor Component responsabile dell'identità dell'entità.

Responsabilità:

- possedere o ricevere `FEntityRelationId`;
- esporre un nome diagnostico opzionale;
- conservare Identity Tags;
- conservare Affiliation o Group Tags, se mantenuti separati;
- registrarsi nel World Subsystem;
- deregistrarsi simmetricamente;
- incrementare la propria revisione quando cambia un dato rilevante;
- notificare l'invalidazione delle relazioni che coinvolgono l'entità;
- offrire API controllate per modificare i tag;
- offrire un controllo locale del debug.

Proprietà suggerite:

```text
EntityId
DebugName
IdentityTags
AffiliationTags
bEnableDebug
```

Non esporre i container mutabili con accesso diretto `BlueprintReadWrite` se questo permette di bypassare revisioni ed eventi.

Preferire API come:

```text
AddIdentityTag
RemoveIdentityTag
SetIdentityTags
AddAffiliationTag
RemoveAffiliationTag
SetAffiliationTags
```

Ogni modifica effettiva deve:

1. aggiornare il dato;
2. incrementare `IdentityRevision`;
3. invalidare le query rilevanti;
4. emettere l'evento appropriato.

Non incrementare revisioni quando il valore finale non cambia.

---

## 7.2 Registrazione e lifecycle

La registrazione deve avvenire in una fase lifecycle verificata e coerente con le convenzioni del progetto.

Non registrare nel costruttore.

La deregistrazione deve essere simmetrica e sicura durante:

- `EndPlay`;
- destruction;
- world teardown;
- unregister del componente;
- PIE stop.

Codex deve verificare se il progetto usa normalmente `BeginPlay/EndPlay` oppure `OnRegister/OnUnregister` per sistemi analoghi e scegliere il ciclo più corretto.

Se si usa `OnRegister`, filtrare correttamente CDO, editor preview e world non gameplay.

---

## 7.3 Duplicazione degli ID

Il subsystem non deve sostituire silenziosamente una registrazione esistente.

Se due entità vive tentano di registrarsi con lo stesso ID logico:

- la seconda registrazione deve fallire;
- deve essere prodotto un log `Error` con entrambe le entità coinvolte;
- la prima registrazione valida deve restare autorevole;
- l'errore deve essere osservabile attraverso il risultato della registrazione;
- il registry non deve entrare in stato ambiguo.

Una entità deregistrata può essere sostituita da una nuova istanza con lo stesso ID logico.

Questo supporta reset, respawn e ricostruzione del mondo.

---

# 8. Stato diretto tra entità

## 8.1 `UEntityRelationStateComponent`

Creare un Actor Component opzionale che appartiene alla Source e conserva solamente stato diretto esplicito.

Struttura concettuale:

```text
Target Entity ID
└── Directed State
    ├── State Tags
    ├── Numeric Values
    └── Revision
```

Lo stato di:

```text
A -> B
```

non modifica automaticamente:

```text
B -> A
```

---

## 8.2 `FEntityDirectedRelationState`

Contenuto minimo suggerito:

```text
StateTags       Gameplay Tag Container
NumericValues   mappa GameplayTag -> float
Revision        contatore runtime
```

Esempi:

```text
Relation.State.Known
Relation.State.Identified
Relation.State.Trusted
Relation.State.TemporarilyHostile
```

Valori numerici possibili:

```text
Relation.Value.Trust
Relation.Value.Suspicion
Relation.Value.Reputation
```

Non aggiungere riferimenti UObject persistenti nello stato diretto della prima versione.

Non serializzare pointer runtime.

---

## 8.3 API controllate

Fornire operazioni esplicite:

```text
HasStateForTarget
GetStateForTarget
SetStateTagsForTarget
AddStateTagForTarget
RemoveStateTagForTarget
SetNumericValueForTarget
RemoveNumericValueForTarget
ClearStateForTarget
ClearAllDirectedState
```

Le operazioni di modifica devono restituire un risultato utile e distinguere:

- modifica avvenuta;
- nessuna modifica necessaria;
- Target ID non valido;
- operazione non consentita;
- stato non trovato.

Ogni modifica effettiva deve:

1. incrementare la revisione della coppia;
2. notificare il subsystem;
3. invalidare le query `Source -> Target`;
4. emettere `OnDirectedRelationStateChanged`.

Se uno stato torna completamente vuoto, rimuovere la entry dalla mappa sparsa.

---

## 8.4 WorldState

`EntityRelations` non deve dipendere da `WorldState`.

Il componente deve però mantenere i propri dati in proprietà riflesse e controllate in modo da poter essere serializzato da un sistema esterno.

Dati candidati al ripristino:

- ID espliciti;
- Identity Tags modificabili;
- Affiliation Tags modificabili;
- stato diretto;
- override di relazione.

Dati derivati da non serializzare:

- registry runtime;
- weak pointer;
- handle runtime;
- cache;
- revisioni ricostruibili;
- risultati delle query;
- spiegazioni diagnostiche.

---

# 9. Policy

## 9.1 `UEntityRelationPolicy`

Creare una classe UObject astratta e instanziabile nei Policy Set.

Scopo:

- valutare una specifica regola;
- leggere Source, Target, componenti e stato diretto;
- contribuire al risultato;
- non modificare lo stato del mondo;
- non conservare stato mutabile per singola query.

Proprietà minime:

```text
PolicyId
Priority
SupportedDomains
bStopEvaluationAfterContribution
bCacheable
bEnabled
```

`PolicyId` deve essere stabile e utile nei log.

`Priority` deve essere un intero con significato esplicito: valore maggiore = valutazione precedente.

`SupportedDomains` limita le query processate dalla policy.

Una policy che non supporta il dominio deve essere saltata senza warning.

---

## 9.2 Implementazione C++ e Blueprint

Fornire un'implementazione nativa completa e sicura.

L'entry point di valutazione deve:

1. validare la query;
2. verificare dominio e configurazione;
3. invocare una funzione protetta sostituibile;
4. validare il contributo restituito;
5. impedire modifiche dirette allo stato interno del subsystem.

Usare `BlueprintNativeEvent` solamente come extension point intenzionale.

Non esporre ogni funzione interna a Blueprint.

Le policy ad alta frequenza o performance-sensitive devono poter essere implementate interamente in C++.

Le policy Blueprint devono essere documentate come meno adatte a query massive per frame.

Verificare le reali firme UE prima di implementare `const`, eventi Blueprint e instancing UObject.

---

## 9.3 `FEntityRelationContribution`

Ogni policy restituisce un contributo separato dal risultato finale.

Contenuto minimo:

```text
Decision
ClassificationTags
OutcomeTags
ReasonTags
bStopEvaluation
```

Una policy può:

- non contribuire;
- aggiungere classificazioni;
- aggiungere outcome;
- esprimere `Allow`;
- esprimere `Deny`;
- richiedere la terminazione della valutazione.

La policy non deve costruire direttamente un `FEntityRelationResult` completo.

---

# 10. Policy Set

## 10.1 `UEntityRelationPolicySet`

Creare un Data Asset che possiede una lista ordinata di policy instanziate.

La lista deve essere configurabile da editor.

Ogni policy deve essere posseduta correttamente dal Data Asset e tracciata dal garbage collector.

Se viene usato il pattern `EditInlineNew`, `DefaultToInstanced` e `UPROPERTY(Instanced)`, verificare gli specifier e il comportamento reale nella versione UE del progetto prima di implementare.

Le policy devono essere considerate configurazione immutabile durante una sessione runtime normale.

Non memorizzare dentro le policy:

- Source corrente;
- Target corrente;
- ultimo risultato;
- dati temporanei condivisi tra query;
- cache per coppia.

---

## 10.2 Validazione del Policy Set

Il Policy Set deve essere validabile.

Controlli minimi:

- policy nulla;
- `PolicyId` non valido;
- `PolicyId` duplicato;
- domini non configurati;
- policy disabilitate;
- priorità duplicate, da consentire ma segnalare come warning se rendono l'ordine poco chiaro;
- configurazioni incompatibili;
- nessuna policy disponibile.

A parità di priorità, usare l'ordine serializzato nel Data Asset come tie-breaker deterministico.

Non usare nomi UObject o ordine non deterministico di una `TMap` per decidere la precedenza.

---

# 11. Risoluzione deterministica delle policy

Il resolver deve seguire questo algoritmo:

1. recuperare le policy abilitate che supportano il dominio;
2. ordinarle per priorità decrescente;
3. mantenere l'ordine del Policy Set a parità di priorità;
4. valutare una policy alla volta;
5. accumulare Classification Tags, Outcome Tags e Reason Tags senza duplicati;
6. usare la prima decisione diversa da `NoOpinion` come decisione autorevole;
7. non permettere a policy di priorità inferiore di sovrascrivere la decisione autorevole;
8. permettere alle policy successive di aggiungere classificazioni e motivazioni finché la valutazione non viene arrestata;
9. arrestare la valutazione quando una policy valida restituisce `bStopEvaluation`;
10. terminare con `NoOpinion` se nessuna policy esprime una decisione.

Esempio:

```text
Priority 1000 — override di livello: Allow
Priority 500  — regola temporale: Deny
Priority 100  — policy di default: NoOpinion
```

Risultato:

```text
Allow
```

perché la prima decisione autorevole è quella con priorità maggiore.

Le policy successive possono aggiungere dati diagnostici, ma non cambiare la decisione.

Non implementare nella prima versione resolver configurabili, graph arbitrari o sistemi di voting se non esiste già un requisito reale nel repository.

---

# 12. World Subsystem

## 12.1 `UEntityRelationsWorldSubsystem`

Il World Subsystem è il coordinatore runtime.

Responsabilità:

- registry degli Identity Component;
- registrazione e deregistrazione;
- risoluzione ID -> componente;
- selezione del Policy Set attivo;
- esecuzione delle query;
- esecuzione delle query batch;
- recupero dello stato diretto;
- revisione e invalidazione;
- cache;
- diagnostica;
- eventi globali del plugin.

Non deve avere Tick.

Non deve essere usato come contenitore generico per logica non correlata.

---

## 12.2 Registry

Usare una struttura coerente con:

```text
FEntityRelationId -> weak reference a UEntityIdentityComponent
```

Il registry non deve mantenere in vita le entità.

Durante ogni accesso:

- verificare la validità della weak reference;
- rimuovere o segnalare entry stale secondo un percorso controllato;
- non dereferenziare UObject durante teardown senza verifiche.

La registrazione deve restituire un risultato strutturato, non un semplice silenzio.

---

## 12.3 Policy Set attivo

La configurazione del Policy Set deve essere esplicita.

Possibili fonti, in ordine da verificare rispetto alle convenzioni del progetto:

- Developer Settings con asset soft reference;
- configurazione per World o Game Mode;
- API esplicita di override sul subsystem;
- provider del progetto.

Non hardcodare un asset path.

Un override runtime deve avere un cleanup simmetrico e non deve restare attivo tra World differenti.

Se non è disponibile un Policy Set:

- la query deve fallire con `MissingPolicySet`;
- produrre un warning utile e non spam;
- non assumere `Allow`.

---

# 13. API pubbliche

## 13.1 Query C++

Fornire API chiare per:

```text
EvaluateRelationById
EvaluateRelationByComponent
EvaluateRelationByActor
EvaluateRelationsFromSource
```

Le API devono:

- validare input;
- restituire sempre `FEntityRelationResult`;
- non richiedere un ordine di chiamata nascosto;
- non mutare lo stato;
- non usare `check()` per errori runtime recuperabili;
- restituire status comprensibili.

---

## 13.2 Query Blueprint

Esporre solamente nodi di alto livello.

Categorie suggerite:

```text
Entity Relations|Query
Entity Relations|Identity
Entity Relations|Directed State
Entity Relations|Debug
```

Le query non devono essere `BlueprintPure` se possono:

- effettuare più valutazioni;
- consultare numerose policy;
- usare una cache;
- produrre diagnostica;
- avere un costo non banale.

Blueprint può rivalutare i nodi pure più volte in modo non evidente.

Restituire il risultato strutturato e, dove utile, un booleano `bSuccess` derivato dallo status tecnico.

---

## 13.3 Blueprint Function Library

Creare una `UEntityRelationsBlueprintLibrary` solamente per wrapper stateless utili, per esempio:

- recuperare Identity Component da Actor;
- eseguire una query attraverso il World Context;
- verificare tag nel risultato;
- convertire status e decisioni in testo diagnostico.

La logica autorevole deve restare nel subsystem e nei componenti.

---

# 14. Query batch

Prevedere un'API batch per casi come:

- percezione di molti target;
- Tactical Analysis;
- validazione di una selezione multipla;
- AI che confronta più candidati.

Forma suggerita:

```text
Source
Array di Target
Context condiviso
Array di risultati nello stesso ordine dei Target
```

Requisiti:

- mantenere l'ordine input/output;
- validare ogni Target indipendentemente;
- non interrompere l'intero batch per un singolo Target invalido;
- evitare allocazioni temporanee superflue;
- usare un unico scope Unreal Insights per il batch;
- non duplicare la logica del resolver;
- riusare il percorso della query singola.

Non introdurre elaborazione async nella prima versione.

Le policy possono accedere a UObject e componenti gameplay, quindi l'esecuzione deve restare sul Game Thread salvo futura architettura esplicitamente thread-safe.

---

# 15. Revisioni e invalidazione

## 15.1 Revisioni

Mantenere revisioni monotone runtime per:

- identità Source;
- identità Target;
- stato diretto della coppia;
- Policy Set o configurazione attiva.

Le revisioni servono a determinare se un risultato cache è ancora valido.

Non usare timestamp wall-clock.

---

## 15.2 Eventi di invalidazione

Eventi globali suggeriti:

```text
OnEntityRegistered
OnEntityUnregistered
OnEntityIdentityChanged
OnDirectedRelationStateChanged
OnRelationsInvalidatedForEntity
OnRelationsInvalidatedForPair
OnPolicySetChanged
```

Evitare un generico `OnRelationChanged` che ricalcola automaticamente tutte le coppie possibili.

Quando cambia l'identità di A, notificare che le relazioni che coinvolgono A sono invalidate.

I sistemi interessati decidono quali query ripetere.

Non effettuare un ricalcolo eager `N x N`.

---

# 16. Cache

## 16.1 Obiettivo

La cache deve ridurre il costo di query ripetute senza diventare una seconda fonte di verità.

La correttezza viene prima dell'ottimizzazione.

Il risultato deve essere sempre ricostruibile dalle entità, dallo stato e dalle policy.

---

## 16.2 Chiave della cache

La chiave logica deve includere almeno:

```text
SourceId
TargetId
Domain
ContextTags hash
NumericContext hash
SourceRevision
TargetRevision
PairRevision
PolicySetRevision
```

Verificare che l'hashing sia deterministico.

Non dipendere dall'ordine interno non garantito di mappe o container.

Se necessario, normalizzare il contenuto prima dell'hash.

Non invertire Source e Target.

---

## 16.3 Condizioni di esclusione

Non usare la cache quando:

- il chiamante imposta `bAllowCache = false`;
- viene richiesta una spiegazione dettagliata non presente nella entry;
- una policy applicabile dichiara `bCacheable = false`;
- la query usa dati non deterministici;
- Source o Target non sono registrati;
- il Policy Set non è valido.

---

## 16.4 Invalidazione

Preferire invalidazione tramite revisioni invece di scansionare e rimuovere immediatamente tutte le entry coinvolte.

La cache può rimuovere entry obsolete:

- durante accesso;
- tramite pulizia periodica non basata su Tick;
- quando supera un limite configurato;
- in occasione di eventi controllati.

Imporre un limite massimo configurabile.

Non permettere crescita illimitata.

La cache deve poter essere:

- disabilitata globalmente;
- svuotata tramite API di debug;
- ispezionata tramite contatori hit/miss.

---

# 17. Developer Settings

Se coerente con il progetto, creare `UEntityRelationsDeveloperSettings`.

Configurazioni possibili:

```text
DefaultPolicySet
bEnableQueryCache
MaxCacheEntries
bEnableGlobalDebug
bEnableVerboseExplanation
bEnableDebugDraw
```

Usare soft reference per asset configurabili quando appropriato.

Non caricare asset in modo sincrono in un hot path.

Definire chiaramente quando e come il Policy Set viene risolto.

Le impostazioni devono avere categorie, tooltip e default sicuri.

---

# 18. Logging

Il modulo deve possedere una sola categoria primaria:

```text
LogEntityRelations
```

Creare macro di modulo almeno per:

```text
ENTITYRELATIONS_LOG_INFO
ENTITYRELATIONS_LOG_WARNING
ENTITYRELATIONS_LOG_ERROR
```

Non usare `LogTemp` nel codice committato.

I log devono includere contesto utile:

- World;
- Source ID;
- Target ID;
- Actor o componente;
- dominio;
- Policy Set;
- Policy ID;
- status;
- decisione.

Evitare log per ogni query normale.

Errori e warning ripetuti devono essere emessi una sola volta, rate-limited o associati a una transizione di stato.

---

# 19. Debug e diagnostica

## 19.1 Controllo globale e locale

Il debug visivo effettivo deve seguire:

```text
GlobalDebugEnabled AND LocalDebugEnabled
```

Il debug globale deve poter disabilitare immediatamente tutto l'output visivo del plugin.

Ogni Identity Component deve poter disabilitare localmente il proprio debug.

Il debug deve essere disabilitato di default.

---

## 19.2 Spiegazione delle query

Fornire un percorso diagnostico che mostri:

- Source;
- Target;
- dominio;
- contesto;
- policy valutate;
- policy saltate e motivo;
- contributo di ogni policy;
- decisione autorevole;
- classificazioni;
- outcome;
- revisioni;
- cache hit/miss;
- status finale.

Il percorso diagnostico non deve cambiare il risultato della query.

---

## 19.3 Comandi di debug

Aggiungere comandi console o funzioni equivalenti, seguendo le convenzioni del progetto:

```text
EntityRelations.List
EntityRelations.Dump <EntityId>
EntityRelations.Explain <SourceId> <TargetId> <Domain>
EntityRelations.ClearCache
EntityRelations.CacheStats
```

Verificare le API reali di registrazione dei comandi console.

Non lasciare comandi registrati senza cleanup durante module shutdown se la loro API lo richiede.

---

## 19.4 Debug visivo

Il debug visivo è opzionale e deve essere leggero.

Possibili elementi:

- linea Source -> Target;
- freccia per mostrare la direzione;
- testo con decisione e dominio;
- colore configurabile in base alla decisione.

Non usare il debug visivo come UI di gioco.

Non disegnare tutte le coppie registrate automaticamente.

Disegnare solamente:

- query selezionate;
- entità con debug locale attivo;
- richieste diagnostiche esplicite.

Controllare lo stato del debug prima di calcolare dati costosi.

---

# 20. Profiling

Aggiungere scope Unreal Insights ai percorsi significativi:

```text
EntityRelations_EvaluateRelation
EntityRelations_EvaluateBatch
EntityRelations_ResolvePolicies
EntityRelations_CacheLookup
```

Non aggiungere scope a getter e operazioni banali.

Aggiungere contatori o statistiche per:

- numero query;
- numero query batch;
- cache hit;
- cache miss;
- policy valutate;
- entità registrate;
- entry di stato diretto;
- entry cache.

Verificare le API di tracing disponibili nella versione UE del progetto.

Non costruire stringhe dinamiche ad alta frequenza solamente per profiling.

---

# 21. Performance

Il subsystem non deve usare Tick.

Evitare nei percorsi di query:

- `GetAllActorsOfClass`;
- world search ripetute;
- allocazioni non necessarie;
- copie complete di grandi container;
- cast ripetuti evitabili;
- logging ad alta frequenza;
- costruzione di stringhe di debug quando non richiesta;
- valutazione Blueprint in batch massivi senza avviso.

Usare il registry per accesso diretto alle entità.

Le policy devono ricevere viste o riferimenti read-only ai dati necessari.

Non ottimizzare introducendo stato duplicato non invalidabile.

---

# 22. Blueprint API e sicurezza

Le proprietà Blueprint devono usare l'accesso minimo necessario.

Preferire:

- `BlueprintReadOnly` per stato osservabile;
- funzioni controllate per modifiche;
- `EditDefaultsOnly` o `EditInstanceOnly` secondo il reale workflow;
- tooltip;
- categorie coerenti;
- edit condition quando utile.

Non esporre:

- mappe interne del subsystem;
- cache mutabile;
- array di policy mutabile a runtime;
- revisioni modificabili;
- weak pointer interni;
- riferimenti mutabili a state struct interne.

Le funzioni devono fallire in modo prevedibile e restituire motivazioni utili.

---

# 23. Networking

Paradox è attualmente trattato come progetto single-player.

Non aggiungere replica automatica nella prima implementazione se non esiste un requisito già presente.

L'architettura non deve però rendere impossibile una futura estensione.

Separare chiaramente:

- dati autorevoli potenzialmente replicabili;
- registry locale;
- cache locale;
- diagnostica locale.

Non replicare risultati di query derivabili salvo futuro requisito misurato.

---

# 24. Integrazione con Paradox

Le classi specifiche di Paradox non devono essere create dentro `EntityRelations`.

Nel modulo del progetto potranno esistere tipi come:

```text
UParadoxTemporalIdentityComponent
UParadoxTemporalOrderingPolicy
UParadoxTemporalVisualPolicy
UParadoxTemporalAudioPolicy
```

## 24.1 Temporal Identity

Il componente di progetto può contenere:

```text
TemporalIndex
TimelineLineageId
TemporalEntityType
bIsCurrentControlledVersion
```

Questi dati non appartengono al componente identità generico.

Una policy di Paradox può recuperarli dai Source e Target Actor oppure dai relativi componenti specifici.

Il modulo di Paradox dipende da `EntityRelations`; il plugin non dipende dal modulo di Paradox.

---

## 24.2 Ordinamento temporale

Regola di classificazione:

```text
TargetIndex < SourceIndex  -> Target è passato rispetto a Source
TargetIndex = SourceIndex  -> stessa posizione temporale
TargetIndex > SourceIndex  -> Target è futuro rispetto a Source
```

Questa policy aggiunge classificazioni, ma non genera un paradosso.

---

## 24.3 Percezione visiva

Semantica della query:

```text
Source = osservatore
Target = entità osservata
Domain = Relation.Domain.VisualPerception
```

Regola di progetto:

```text
stessa TimelineLineage
AND TargetIndex > SourceIndex
-> Relation.Outcome.ParadoxCandidate
```

Il sistema di percezione:

1. trova geometricamente un Target;
2. verifica distanza, cono e line of sight;
3. interroga `EntityRelations`;
4. legge l'Outcome;
5. gestisce tempo minimo di rilevamento;
6. notifica il sistema di Paradox.

`EntityRelations` non genera il paradosso.

---

## 24.4 Percezione audio

Semantica della query:

```text
Source = ascoltatore
Target = entità che ha prodotto il rumore
Domain = Relation.Domain.AudioPerception
```

La policy può produrre un Outcome come:

```text
Relation.Outcome.ValidTemporalDistraction
```

La propagazione sonora e la reazione dell'agente restano esterne.

---

## 24.5 Tactical Analysis

Il sistema tattico deve poter effettuare query senza effetti collaterali per classificare altri cloni rispetto a quello selezionato.

Può usare:

```text
Relation.Domain.TacticalPreview
```

oppure una query visiva esplicitamente configurata per preview.

La UI decide colori, icone e testi.

Il plugin restituisce solamente dati semantici.

---

# 25. Integrazione con altri plugin

## 25.1 GoalAgents

`GoalAgents` può interrogare relazioni per:

- validare target;
- filtrare candidati;
- decidere se un Goal è consentito;
- classificare entità conosciute.

`EntityRelations` non deve dipendere da `GoalAgents`.

## 25.2 IterationLoop

`IterationLoop` può:

- assegnare ID logici;
- ricreare entità con lo stesso ID;
- assegnare dati temporali di progetto;
- registrare nuove versioni temporali.

`EntityRelations` non deve creare iterazioni o cloni.

## 25.3 WorldState

`WorldState` può serializzare dati riflessi dei componenti.

`EntityRelations` deve ricostruire registry e cache dopo il reset.

Non salvare la cache.

## 25.4 Sistemi di percezione o combattimento

Questi sistemi determinano:

- chi è candidato;
- se un hit è avvenuto;
- se una linea visiva è valida;
- se un attacco è stato richiesto.

Poi interrogano `EntityRelations` per classificazione o permesso.

---

# 26. Test automatici richiesti

Aggiungere test runtime o automation test coerenti con le convenzioni del repository.

Casi minimi:

## Identità e registry

- registrazione valida;
- deregistrazione valida;
- ID invalido;
- ID duplicato;
- sostituzione dopo deregistrazione;
- weak reference stale;
- teardown del World.

## Direzionalità

- `A -> B` diverso da `B -> A`;
- stato diretto memorizzato solo sulla Source;
- nessuna normalizzazione automatica della coppia.

## Policy

- dominio supportato;
- dominio non supportato;
- policy disabilitata;
- priorità decrescente;
- tie-breaker tramite ordine del Policy Set;
- prima decisione autorevole;
- policy successive non sovrascrivono la decisione;
- accumulo tag senza duplicati;
- arresto con `bStopEvaluation`;
- nessuna policy decisiva produce `NoOpinion`.

## Stato diretto

- aggiunta e rimozione tag;
- valori numerici;
- rimozione entry vuota;
- revisione incrementata solo su modifica;
- evento emesso solo su modifica effettiva.

## Cache

- cache hit su query identica;
- miss su dominio differente;
- miss su contesto differente;
- invalidazione dopo modifica Source;
- invalidazione dopo modifica Target;
- invalidazione dopo modifica dello stato della coppia;
- invalidazione dopo cambio Policy Set;
- policy non cacheabile;
- limite massimo entry;
- `bAllowCache = false`.

## Error handling

- Missing Policy Set;
- Source non registrata;
- Target non registrata;
- spiegazione richiesta;
- query batch con Target parzialmente invalidi.

## Assenza di side effect

- la query non modifica identità;
- la query non modifica stato diretto;
- la query non emette eventi di gameplay;
- la query ripetuta produce lo stesso risultato a stato invariato.

---

# 27. Documentazione utente richiesta

Creare almeno:

```text
Plugins/EntityRelations/Docs/README.md
```

Il README deve spiegare:

- scopo del plugin;
- concetto Source -> Target;
- differenza tra identità, stato, policy e risultato;
- setup;
- creazione di un Policy Set;
- aggiunta di Identity Component;
- query C++;
- query Blueprint;
- stato diretto;
- cache;
- debug;
- extension point;
- integrazione con moduli esterni;
- limiti noti.

Per un plugin sufficientemente ampio, separare anche:

```text
ARCHITECTURE.md
BLUEPRINT_API.md
CPP_API.md
DEBUGGING.md
EXTENDING.md
```

Non duplicare le istruzioni per Codex nella documentazione per utenti.

---

# 28. Ordine di implementazione consigliato

Implementare in questo ordine:

## Fase 1 — Fondazioni

- plugin e modulo;
- log category e macro;
- `FEntityRelationId`;
- enum e struct di query/risultato;
- Gameplay Tags nativi o convenzioni di tag secondo il progetto.

## Fase 2 — Identità e registry

- `UEntityIdentityComponent`;
- `UEntityRelationsWorldSubsystem`;
- registrazione;
- deregistrazione;
- duplicate detection;
- eventi di registry.

## Fase 3 — Policy

- `UEntityRelationPolicy`;
- `FEntityRelationContribution`;
- `UEntityRelationPolicySet`;
- resolver deterministico;
- Policy Set di test.

## Fase 4 — Query

- API C++;
- Blueprint Function Library;
- query per ID, componente e Actor;
- status e motivazioni;
- query batch.

## Fase 5 — Stato diretto

- `UEntityRelationStateComponent`;
- API controllate;
- revisioni;
- eventi;
- collegamento alle policy.

## Fase 6 — Cache

- chiave deterministica;
- revisioni;
- limite massimo;
- statistiche;
- clear cache;
- test di invalidazione.

## Fase 7 — Debug e profiling

- Developer Settings;
- Explain Query;
- console commands;
- debug visivo opzionale;
- Unreal Insights;
- contatori.

## Fase 8 — Documentazione e validazione

- Docs;
- test completi;
- compilazione;
- revisione del diff;
- verifica Blueprint;
- controllo lifecycle e teardown.

---

# 29. Criteri di completamento

Il task è completato solamente quando:

- il plugin rispetta `AGENTS.md` e i `CODEX` locali;
- il core non contiene riferimenti specifici a Paradox;
- Source e Target non vengono mai scambiati implicitamente;
- le query sono prive di effetti collaterali;
- identità, stato, policy e risultato sono separati;
- il registry rifiuta ID duplicati senza corrompersi;
- lifecycle e cleanup sono simmetrici;
- il resolver è deterministico;
- la prima decisione con priorità maggiore è autorevole;
- le policy successive non possono sovrascriverla;
- le query invalide restituiscono status espliciti;
- `NoOpinion` non viene convertito implicitamente in `Allow`;
- lo stato diretto è sparso e asimmetrico;
- la cache è limitata, revisionata e disabilitabile;
- non è stato aggiunto Tick;
- non sono state introdotte world search ad alta frequenza;
- il debug ha controllo globale e locale;
- il modulo usa `LogEntityRelations` e macro dedicate;
- i percorsi costosi sono misurabili in Unreal Insights;
- gli Automation Test richiesti passano;
- il target interessato compila senza errori;
- la documentazione nel folder `Docs` è completa e aggiornata;
- il diff finale non contiene modifiche estranee.

Se il codice non compila, il task non è completato.

---

# 30. Vincoli finali

Non implementare scorciatoie specifiche del gioco nel core.

Non trasformare il subsystem in un god object.

Non applicare conseguenze durante le query.

Non memorizzare una matrice completa di relazioni.

Non usare pointer UObject come identità logica.

Non permettere modifiche dirette ai container interni senza revisioni e invalidazione.

Non introdurre async finché le policy accedono a UObject gameplay non thread-safe.

Non aggiungere replica, editor graph o resolver complessi senza un requisito concreto.

L'obiettivo è realizzare un motore di relazioni generico, prevedibile e difficile da usare in modo scorretto, mantenendo le regole specifiche di Paradox nel modulo di integrazione del progetto.
