# LineOfSight 1.2

Documentazione locale del plugin Marketplace `LineOfSight` per Unreal Engine 5.8.

## Scopo

Il plugin aggiunge `ULineOfSightComponent`, un componente Blueprint-spawnable derivato da `UProceduralMeshComponent`. Il componente campiona un'area di visione con una serie di `LineTraceMultiByChannel`, mantiene lo stato degli Actor rilevati e, facoltativamente, genera una mesh procedurale che si arresta sugli ostacoli.

Il sistema può essere usato per:

- coni o settori di visuale per AI e gameplay;
- aree di rilevamento orientabili;
- indicatori visivi che seguono gli ostacoli;
- query istantanee su un settore o una fascia;
- rotazione e interpolazione animate del campo visivo.

La direzione frontale del componente è il suo asse locale `+X`. Angoli e raggi sono espressi rispettivamente in gradi e Unreal Unit.

## Requisiti e contenuto

- Versione dichiarata dal plugin: `1.2`.
- Engine dichiarato: Unreal Engine `5.8.0`.
- Modulo runtime: `LineOfSight`.
- Dipendenza: plugin/modulo `ProceduralMeshComponent`.
- Piattaforme ammesse dal descriptor: Win64, Linux, macOS, iOS e Android.
- Il plugin contiene sorgenti C++, binari Win64/macOS, materiali e texture di esempio.

Il descriptor abilita automaticamente la dipendenza `ProceduralMeshComponent`. Per usare le risorse incluse nell'Editor, attivare **Show Plugin Content** nel Content Browser. Il mount point dei contenuti è `/LineOfSight/`.

## Avvio rapido in Blueprint

1. Aggiungere un componente **Line Of Sight** all'Actor che deve possedere la visuale.
2. Lasciare la scala del componente a `(1, 1, 1)` e orientare l'asse `+X` verso la direzione frontale.
3. Configurare `Geometry Type`, `Angle 1`, `Angle 2`, `Radius 1`, `Radius 2` e il collision channel da interrogare.
4. In `BeginPlay`, chiamare `Start Line Trace`.
5. Se serve la visualizzazione, assegnare `Material` e chiamare `Start Build Mesh` subito dopo `Start Line Trace`.
6. Collegarsi agli eventi `Begin Overlap` e `End Overlap` se servono notifiche persistenti di ingresso e uscita.
7. In fase di arresto usare `Set Pause Trace` oppure `Stop Line Trace`, scegliendo consapevolmente se emettere gli eventi di uscita.

Ordine minimo consigliato:

```text
BeginPlay
  -> StartLineTrace(TraceChannel, NumberOfLines)
  -> StartBuildMesh()                    [solo se serve la mesh]
```

`StartBuildMesh` chiamato prima di `StartLineTrace` non viene eseguito e produce un errore nel log.

## Modello geometrico

Ogni aggiornamento esegue `NumberOfLines + 1` line trace. Il numero passato a `StartLineTrace` viene forzato ad almeno `1`.

### Arc

`Arc` collega campioni distribuiti su due archi:

- `Radius 1` e `Angle 1` descrivono il bordo interno;
- `Radius 2` e `Angle 2` descrivono il bordo esterno;
- ogni trace parte dal bordo interno e termina sul bordo esterno;
- la mesh usa il risultato finale del multi-trace, normalmente il blocking hit, per arrestarsi sull'ostacolo.

Con `Only One Arc` il bordo interno viene sostituito dall'origine del componente. Il risultato è un settore/cone che parte dal centro e usa soltanto il bordo esterno.

`Reverse Arch 1` inverte il verso del primo arco. È utile per ottenere una fascia con bordo interno concavo/rovesciato.

### Line

`Line` campiona tra due segmenti trasversali costruiti ai raggi configurati. Può produrre una fascia o un trapezio invece di due bordi curvi. Anche in questa modalità `Only One Arc` sposta l'origine dei trace al centro del componente.

### Mesh e collisione

La mesh resta una rappresentazione visiva e senza collisione per default, preservando il
comportamento 1.1. La collisione procedurale è ora opt-in tramite
`bEnableDynamicMeshCollision`/`SetDynamicMeshCollisionEnabled`:

- i risultati dei trace vengono confrontati con l'ultima geometria inviata; se nessun vertice si
  sposta oltre `MeshUpdatePositionTolerance`, l'upload ridondante della sezione viene saltato;
- `false` crea la sezione senza collisione e mantiene il profilo iniziale `NoCollision`;
- `true` crea o ricrea la sezione collisionabile e aggiorna in modo sincrono il body fisico;
- il body usa l'unione esatta di prismi triangolari semplici, uno per triangolo visibile, estrusi
  di `DynamicMeshCollisionHalfThickness` sopra e sotto il piano della mesh. Il default e' `5.0`;
- `RefreshLineTraceAndMesh` esegue immediatamente trace, mesh, collisione e refresh degli overlap;
- `SetPauseTrace` e `SetPauseBuildMesh` rimuovono collisione e overlap fisici durante la pausa e li
  ricostruiscono sincronicamente alla ripresa;
- `StopBuildMesh`, `StopLineTrace` e teardown rimuovono sezioni e overlap fisici.

Il consumer possiede ancora `CollisionEnabled`, object type, responses e
`SetGenerateOverlapEvents`. Per overlap fisici configurare normalmente `QueryOnly`, un object type
e risposte reciproche `Overlap`. `bUseAsyncCooking = false` è consigliato quando una barriera di
gameplay richiede che collisione e overlap siano pronti nello stesso frame logico.

Gli eventi custom `BeginOverlap`/`EndOverlap` del plugin restano derivati dai trace e sono distinti
dai veri `OnComponentBeginOverlap`/`OnComponentEndOverlap` della mesh. Un sistema autorevole deve
scegliere esplicitamente quale sorgente usare.

## Proprietà principali

| Proprietà | Default | Funzione |
| --- | ---: | --- |
| `GeometryType` | `Arc` | Sceglie bordi curvi (`Arc`) o lineari (`Line`). Configurare prima di avviare i trace. |
| `Angle1` | `40` | Semiapertura del bordo interno. |
| `Angle2` | `30` | Semiapertura del bordo esterno. |
| `Radius1` | `70` | Distanza del bordo interno. |
| `Radius2` | `1000` | Distanza massima/bordo esterno. |
| `OnlyOneArc` | `false` | Fa partire i trace dall'origine invece che dal bordo interno. Impostare prima di `StartLineTrace`. |
| `ReverseArch1` | `false` | Inverte il primo arco nella geometria `Arc`. |
| `Only_Z_Rotation` | `false` | Nella geometria ad arco applica soltanto lo Yaw alla direzione dei trace. |
| `TraceComplex` | `false` | Usa complex tracing. |
| `IgnoreOwnerActorInTraceLine` | `true` | Ignora l'Actor proprietario quando il trace viene avviato. |
| `BeginAndEndOverlapEvent` | `true` | Mantiene lo stato degli Actor visti ed emette gli eventi di ingresso/uscita. |
| `FrameTracing` | `EveryTick` | Esegue i trace ogni frame oppure solo sui frame pari/dispari. |
| `MeshUpdatePositionTolerance` | `0.1` | Spostamento locale minimo, in cm, necessario per reinviare i vertici della sezione al renderer. Non riduce o sospende i trace. |
| `Material` | nessuno | Materiale applicato alla sezione `0` della mesh. |
| `bEnableDynamicMeshCollision` | `false` | Crea la sezione procedurale con collisione fisica opt-in. |
| `DynamicMeshCollisionHalfThickness` | `5.0` | Semispessore dei prismi triangolari usati dall'overlap fisico. |
| `Type_Of_Triangles` | `Left -> Right` | Winding dei triangoli della mesh; utile se il materiale o il back-face culling mostrano il lato sbagliato. |

### Debug editor-only

Le proprietà seguenti esistono soltanto nelle build con dati editor:

| Proprietà | Default | Funzione |
| --- | ---: | --- |
| `Debug` | `false` | Disegna le linee dei trace continui. |
| `DebugLineThickness` | `2.0` | Spessore delle linee. |
| `DebugAOE` | `true` | Disegna le query one-shot `StartOAEArc` e `StartOAEFlat`. |
| `TimeDebugAOE` | `3.0` | Durata delle linee delle query one-shot. |

Il disegno richiede sia il flag locale `Debug` sia il CVar globale `LineOfSight.Debug 1`. Impostare
`LineOfSight.Debug 0` disabilita immediatamente tutto il debug visivo del plugin. Questi controlli
non sono disponibili come sistema di debug runtime nelle build non-editor.

## Rilevamento ed eventi

### `BeginOverlap`

Evento multicast con un `FHitResult`. Viene emesso la prima volta che un Actor compare nei risultati dei trace attivi. Non è un vero `OnComponentBeginOverlap`: è uno stato calcolato dai trace.

### `EndOverlap`

Evento multicast con un `FHitResult`. Viene emesso quando un Actor precedentemente rilevato non compare più nell'aggiornamento corrente.

Il confronto è effettuato per Actor, quindi un Actor colpito da più linee o componenti genera un solo ingresso nello stato corrente. Il `FHitResult` conservato è quello usato al primo ingresso.

Con `FrameTracing` impostato sui frame pari o dispari, anche l'aggiornamento degli eventi avviene soltanto nei frame in cui vengono eseguiti i trace.

### API dello stato

- `SetBeginAndEndOverlapEvent(bool)`: abilita/disabilita il tracking e svuota lo stato precedente.
- `GetBeginAndEndOverlapEvent()`: restituisce l'impostazione corrente.
- `GetOverlappedActors()`: restituisce in realtà un array di `FHitResult`, uno per Actor attualmente registrato.
- `LineOfSightIsActive()`: indica se `StartLineTrace` è attivo.
- `MeshIsBuilt()`: indica se l'aggiornamento della mesh è attivo.

## Controllo dei trace

### Avvio e arresto

- `StartLineTrace(TraceChannel, NumberOfLines = 60)`: inizializza il canale, i parametri e gli array. Non crea automaticamente la mesh.
- `SetFrameTracing(...)`: cambia a runtime la cadenza dei trace tra ogni frame, frame pari e frame dispari.
- `StopLineTrace()`: arresta trace e mesh, cancella vertici, ignored objects e stato degli overlap.
- `StartBuildMesh()`: crea la sezione procedurale e ne abilita l'aggiornamento. Richiede trace già avviati.
- `StopBuildMesh()`: cancella le sezioni e arresta l'aggiornamento visivo; i trace possono continuare.
- `SetDynamicMeshCollisionEnabled(bool)`: abilita/disabilita collisione sulla sezione e la ricrea
  immediatamente quando la mesh è già costruita.
- `IsDynamicMeshCollisionEnabled()`: restituisce il flag opt-in, non l'intero stato delle collision
  responses.
- `RefreshLineTraceAndMesh()`: forza un aggiornamento deterministico. Restituisce `false` se i
  trace non sono stati avviati.
- `SetVisibilityOfMesh(bool)`: mostra o nasconde la sezione mesh `0` senza cambiare i trace.
- `SetTickEnable(bool)`: chiama prima `StopLineTrace`, poi abilita/disabilita il tick. Riabilitare il tick non riavvia automaticamente i trace.

### Pausa

`SetPauseTrace(Pause, RunEndOverlap, EmptyOverlapArray, ChangeVisibility = true)` evita il costo di un ciclo stop/start completo:

- quando `Pause` è `true`, sospende trace e aggiornamento della mesh;
- le animazioni di rotazione e interpolazione continuano, perché vengono elaborate prima del controllo di pausa;
- `RunEndOverlap` emette `EndOverlap` per tutti gli Actor registrati e poi svuota lo stato;
- `EmptyOverlapArray` svuota lo stato senza eventi quando `RunEndOverlap` è `false`;
- `ChangeVisibility` sincronizza la visibilità della mesh con lo stato di pausa.
- con collisione dinamica abilitata, la pausa pulisce anche body e overlap fisici; questi vengono
  ricostruiti sincronicamente quando sia trace sia mesh non sono più in pausa.

`SetPauseBuildMesh(Pause, ChangeVisibility = true)` sospende/riprende l'aggiornamento visivo e
l'eventuale collisione dinamica. Usarlo dopo `StartBuildMesh`; i trace restano attivi.

`IsPauseTrace()` e `IsPauseBuildMesh()` restituiscono i rispettivi stati di pausa.

### Canale, complessità e Actor ignorati

- `SetTraceChannel(...)`: cambia il canale usato dai trace continui.
- `SetTraceComplex(bool)`: aggiorna immediatamente la modalità complex del trace continuo.
- `AddIgnoredActor(...)` / `AddActorsToIgnore(...)`: aggiunge esclusioni alla query corrente.
- `ClearActorsToIgnore(NewIgnoreSelf = true)`: cancella le esclusioni e, se richiesto, reinserisce l'owner.
- `SetIgnoreSelfLineTrace(bool)`: aggiorna la preferenza usata dai successivi avvii.

`StartLineTrace` ricrea i parametri e cancella gli Actor ignorati manualmente. Aggiungere quindi gli Actor personalizzati **dopo** `StartLineTrace`. Configurare `IgnoreOwnerActorInTraceLine` prima dell'avvio.

La qualità del risultato dipende dalle collision responses del canale selezionato. Per esclusioni stabili è preferibile configurare i collision preset degli oggetti anziché popolare grandi liste manuali.

## Query one-shot

Le query one-shot restituiscono al massimo un `FHitResult` per Actor, non modificano lo stato degli eventi persistenti e non avviano il tick del sistema.

- `StartOAEArc(TraceChannel, Radius1, Radius2, Angle1, Angle2, NumberOfLines = 40)`: esegue immediatamente una query ad arco usando i parametri passati.
- `StartOAEFlat(TraceChannel, NumberOfLines = 40)`: esegue immediatamente la query lineare usando raggi e angoli correnti del componente.

Per entrambe passare sempre `NumberOfLines >= 1`: queste funzioni non applicano il clamp presente in `StartLineTrace`.

Le query one-shot rispettano `TraceComplex` e `IgnoreOwnerActorInTraceLine`, ma non riutilizzano le esclusioni aggiunte alla query continua.

## Angoli, raggi e interpolazione

- `SetAngle1`, `SetAngle2`, `GetAngle1`, `GetAngle2`.
- `SetRadius1`, `SetRadius2`, `GetRadius1`, `GetRadius2`.
- `StartInterpAngle(NewAngle1, NewAngle2, Speed1, Speed2)` e `StopInterpAngle()`.
- `StartInterpRadius(NewRadius1, NewRadius2, Speed1, Speed2)` e `StopInterpRadius()`.
- `SetTolerance(float)`: tolleranza usata per completare interpolazioni e rotazioni; default `0.00005`.

Le interpolazioni vengono elaborate dal tick soltanto mentre i trace sono attivi. Se si desidera animare la forma senza rilevamento, il plugin non offre un percorso di tick indipendente.

## Rotazione

Gli assi del plugin corrispondono a:

- `Z`: Yaw;
- `Y`: Pitch;
- `X`: Roll.

API disponibili:

- `StartRotateInRangeAxis(Axis, Angle, Speed, TypeRotation, NegativeToPositive)`: oscilla tra `-Angle` e `+Angle` rispetto all'orientamento iniziale. Il commento sorgente indica un massimo previsto di `89` gradi.
- `StartRotateToAngleAxis(Axis, Angle, Speed, TypeRotation, AddToCurrent = true)`: ruota verso un angolo relativo o imposta il valore assoluto dell'asse.
- `StartRotateToActor(Axis, Actor, Speed)`: calcola una rotazione verso l'Actor e usa la rotazione world.
- `StopRotateToAngle`, `StopRotateInRange`, `StopAllRotate`.
- `FindAngleRotate(Actor)`: restituisce la rotazione look-at dal componente all'Actor; un Actor non valido produce `ZeroRotator`.

`TypeRotation` può essere `Relative Rotation` oppure `World Rotation`. Le rotazioni vengono aggiornate soltanto mentre `StartLineTrace` è attivo, anche quando i trace sono in pausa tramite `SetPauseTrace`.

`RotateToAngleEnd` viene emesso quando una rotazione verso un angolo raggiunge il target entro `Tolerance`.

## Vertici, normali e clonazione

### Vertici

- `GetVertexArrayLocalPositionNoRotation()`: vertici grezzi della geometria locale.
- `GetVertexArrayLocalPosition()`: vertici ruotati con la rotazione del componente, senza traslazione.
- `GetVertexArrayWorldPosition()`: vertici ruotati e traslati nella posizione del componente.
- `SetNormals(Vector)`: sostituisce le normali del buffer mentre i trace sono attivi; usarlo dopo la creazione della mesh.

I calcoli dei trace usano posizione e rotazione, ma non applicano la scala del componente. Una scala diversa da `(1, 1, 1)` può quindi far divergere mesh e trace.

### Clonazione

- `ZStartCloneTo(OtherLineOfSightComponent)`: crea sul componente destinazione una sezione con la topologia del sorgente e ne aggiorna rotazione e vertici.
- `ZStopCloneTo()`: interrompe l'aggiornamento della copia.

Per un clone affidabile, avviare prima trace e mesh sul sorgente, assegnare un `Material` anche al componente destinazione e poi chiamare `ZStartCloneTo`. La clonazione viene aggiornata dal tick del sorgente e si arresta se la destinazione non è più valida.

## Materiali inclusi

Il plugin include asset nelle cartelle:

```text
/LineOfSight/Material/
/LineOfSight/Textures/
```

Tra gli asset sono presenti `MI_Simple_material`, vari materiali/istanze per maschere e post process, e texture circolari. Il componente assegna automaticamente la proprietà `Material` alla sezione `0` quando viene chiamato `StartBuildMesh`; nessun materiale viene scelto automaticamente.

Per la mesh procedurale usare un materiale compatibile con una surface mesh. Gli asset post-process inclusi richiedono invece la normale configurazione di un Post Process Volume o di una blendable e non vengono collegati dal componente.

## Prestazioni e profiling

Il costo principale è dato da `NumberOfLines + 1` multi-trace per aggiornamento. Aumentare la risoluzione rende il bordo sugli ostacoli più preciso ma aumenta il costo CPU e il numero di vertici aggiornati.

La sezione procedurale viene aggiornata soltanto quando almeno un vertice locale cambia oltre
`MeshUpdatePositionTolerance`. Un componente che si muove in spazio libero continua quindi a
tracciare e resta pronto a deformarsi, ma non reinvia ogni frame una geometria locale identica.

Indicazioni pratiche:

- iniziare con `20-40` linee per indicatori piccoli e salire solo se il profilo visivo lo richiede;
- usare i frame pari/dispari per dimezzare la frequenza dei trace;
- disabilitare `BeginAndEndOverlapEvent` se servono soltanto mesh o vertici;
- usare `OnlyOneArc` quando non serve un bordo interno;
- mettere in pausa trace o mesh quando non visibili;
- evitare `TraceComplex` se i collision proxy semplici sono sufficienti;
- non usare liste molto grandi di Actor ignorati se è possibile risolvere il filtro con i collision channel.

Il modulo registra il gruppo statistiche `LineOfSight`, con cicli separati per tick complessivo e line trace. In una sessione con stats abilitate usare:

```text
stat LineOfSight
```

Sono inoltre presenti CPU profiler scopes per tick, build della mesh, trace continui e query one-shot.

## Comportamenti da conoscere nella versione 1.2

- `StopLineTrace` svuota lo stato degli overlap senza emettere `EndOverlap`. Se i consumer devono ricevere l'uscita, chiamare prima `SetPauseTrace(true, true, false)`.
- `StartLineTrace` cancella le esclusioni manuali: aggiungerle dopo l'avvio.
- Per smettere di ignorare l'owner durante una query già attiva, usare `ClearActorsToIgnore(false)`; cambiare soltanto la preferenza ha effetto affidabile al successivo riavvio dei trace.
- `SetGeometryType` cambia modalità soltanto quando i trace non sono attivi. Anche `OnlyOneArc` e la topologia dei triangoli vanno configurati prima dell'avvio/build.
- `Test1` è esposta a Blueprint ma non esegue alcuna operazione in questa versione.
- La proprietà Blueprint `TypeTriangle` non viene letta dalla generazione della mesh; la proprietà usata è `Type_Of_Triangles` nel pannello Details.
- `FResultLineTrace` è dichiarata dall'API, ma le funzioni pubbliche correnti restituiscono `FHitResult` o array di `FHitResult`.
- Il plugin non implementa RPC o sincronizzazione di rete. In multiplayer, decidere esplicitamente su quale macchina eseguire trace, eventi e visualizzazione.
- La collisione dinamica è deliberatamente disabilitata per default. Abilitare solo il flag non
  sostituisce la configurazione `QueryOnly`, responses reciproche e `GenerateOverlapEvents`.

Il modulo usa una sola categoria `LogLineOfSight` e le macro
`LINEOFSIGHT_LOG_INFO/WARNING/ERROR`; non usa `LogTemp` nel codice runtime.

## Integrazione C++

Nel `Build.cs` del modulo consumer aggiungere `LineOfSight` alle dipendenze appropriate, poi includere:

```cpp
#include "LineOfSightComponent.h"
```

Il componente può essere creato come default subobject:

```cpp
LineOfSightComponent = CreateDefaultSubobject<ULineOfSightComponent>(TEXT("LineOfSightComponent"));
LineOfSightComponent->SetupAttachment(RootComponent);
```

Avviare i trace in una fase con `UWorld` valido, normalmente `BeginPlay`, e chiamare `StopLineTrace` quando il sistema non è più necessario. Per usare il componente da C++, applicare gli stessi vincoli di ordine descritti per Blueprint.

## Struttura del plugin

```text
LineOfSight/
├── LineOfSight.uplugin
├── Source/LineOfSight/
│   ├── Public/LineOfSight.h
│   ├── Public/LineOfSightComponent.h
│   ├── Private/LineOfSight.cpp
│   └── Private/LineOfSightComponent.cpp
├── Content/
│   ├── Material/
│   └── Textures/
├── Resources/
└── Docs/README.md
```

## Risoluzione dei problemi

### La mesh non è visibile

Verificare che:

1. `StartLineTrace` sia stato chiamato prima di `StartBuildMesh`;
2. `Material` sia assegnato;
3. la sezione mesh sia visibile e non in pausa;
4. la scala del componente sia `(1, 1, 1)`;
5. il winding `Type_Of_Triangles` sia adatto al lato dal quale si osserva la mesh;
6. raggi, angoli e `NumberOfLines` producano una geometria non degenere.

### Il campo visivo attraversa un oggetto

Controllare il collision channel passato a `StartLineTrace`, le collision responses dell'oggetto e `TraceComplex`. Il componente usa multi-trace per channel: un oggetto che ignora quel canale non può arrestare la mesh.

### Gli eventi non arrivano

Controllare che `BeginAndEndOverlapEvent` sia attivo, che i trace non siano fermi, che l'Actor risponda al canale e che non sia presente nelle esclusioni. Ricordare che gli eventi sono prodotti dai trace, non dalla collisione della mesh.

### Gli overlap fisici della mesh non arrivano

Verificare separatamente:

1. `bEnableDynamicMeshCollision = true`;
2. trace avviati e mesh costruita;
3. `RefreshLineTraceAndMesh()` riuscito;
4. `CollisionEnabled = QueryOnly`;
5. `GenerateOverlapEvents = true` su entrambi i componenti;
6. object channel e responses reciproche configurati su `Overlap`.

Non usare `GetOverlappedActors()` per verificare questo percorso: quella funzione espone lo stato
trace-derived. Usare le API standard `GetOverlappingActors/Components` o i delegate fisici del
`UPrimitiveComponent`.

### La rotazione o l'interpolazione non parte

Queste funzioni vengono elaborate soltanto se `LineOfSightIsActive()` è `true`. Chiamare prima `StartLineTrace` e verificare che il tick del componente sia abilitato.

### Dopo una pausa non ricevo un nuovo BeginOverlap

Se lo stato precedente non è stato svuotato, gli Actor già registrati non generano un nuovo ingresso. Usare gli argomenti `RunEndOverlap` o `EmptyOverlapArray` di `SetPauseTrace` in base alla semantica desiderata.

---

Questa documentazione descrive il comportamento osservato nei sorgenti locali della versione
`1.2`; in caso di aggiornamento del plugin, verificare nuovamente descriptor e API prima di
considerarla ancora valida.
