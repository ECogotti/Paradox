#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Environment/ParadoxAccretionExtension.h"
#include "Environment/ParadoxBlackHole.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataSetAccessor.h"

namespace
{
    struct FAccretionTestWorld
    {
        UWorld* World = nullptr;
        FAccretionTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AccretionLifecycleTest"));
            World->AddToRoot();
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            Context.OwningGameInstance = NewObject<UGameInstance>(GEngine);
            World->SetGameInstance(Context.OwningGameInstance);
        }
        ~FAccretionTestWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            World->DestroyWorld(true);
            GEngine->DestroyWorldContext(World);
            World->RemoveFromRoot();
        }
        void StartPlay()
        {
            World->CreateAISystem();
            World->SetGameMode(FURL());
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!It->HasActorBegunPlay()) { It->DispatchBeginPlay(); }
            }
        }
    };

    TArray<float> ReadSeeds(UNiagaraComponent* Component)
    {
        TArray<float> Result;
        const FNiagaraSystemInstanceControllerPtr Controller = Component->GetSystemInstanceController();
        if (!Controller.IsValid() || !Controller->IsValid() || !Controller->IsSolo()) { return Result; }
        for (const FNiagaraEmitterInstanceRef& Emitter : Controller->GetSoloSystemInstance()->GetEmitters())
        {
            FNiagaraDataSet& Data = Emitter->GetParticleData();
            const auto Reader = FNiagaraDataSetAccessor<float>(Data, TEXT("MaterialRandom")).GetReader(Data.GetCurrentData());
            if (!Reader.IsValid()) { return Result; }
            for (int32 I = 0; I < Emitter->GetNumParticles(); ++I) { Result.Add(Reader.Get(I)); }
        }
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxAccretionLifecycleTest,
    "Paradox.Accretion.SeedsSwitchesAndLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxAccretionLifecycleTest::RunTest(const FString& Parameters)
{
    FAccretionTestWorld Scope;
    AParadoxBlackHole* Source = Scope.World->SpawnActor<AParadoxBlackHole>();
    Source->SetRefractionStrength(0);
    AParadoxAccretionExtension* Extension = Scope.World->SpawnActor<AParadoxAccretionExtension>();
    Extension->SetSourceBlackHole(Source);
    Scope.StartPlay();
    TestTrue(TEXT("Begins play with real particle populations"), Extension->HasActorBegunPlay() && Extension->bActive);
    const TArray<UNiagaraComponent*> Components = Extension->GetParticleComponents();
    TArray<TArray<float>> Initial;
    const int32 Counts[] = {16, 24, 96, 16, 24, 96};
    for (int32 I = 0; I < Components.Num(); ++I)
    {
        Initial.Add(ReadSeeds(Components[I]));
        TestEqual(TEXT("Actual seed count"), Initial[I].Num(), Counts[I]);
        TestTrue(TEXT("Static seed simulation paused"), Components[I]->IsPaused() && !Components[I]->IsComponentTickEnabled());
    }
    for (int32 I = 0; I < 3; ++I) { TestTrue(TEXT("Foreground and midground share identical field"), Initial[I] == Initial[I+3]); }
    Extension->SetEnabled(false);
    TestFalse(TEXT("Off has no active populations"), Extension->bActive);
    Extension->SetEnabled(true);
    for (int32 I = 0; I < Components.Num(); ++I) { TestTrue(TEXT("Restart preserves field seeds"), ReadSeeds(Components[I]) == Initial[I]); }
    Extension->SetGasEnabled(false);
    TestFalse(TEXT("Gas stops immediately"), Components[0]->IsActive() || Components[3]->IsActive());
    TestTrue(TEXT("Filaments continue independently"), Components[1]->IsActive() && Components[4]->IsActive());
    Extension->SetGasEnabled(true);
    Extension->SetQuality(EParadoxAccretionQuality::High);
    TestEqual(TEXT("High creates real gas count"), ReadSeeds(Components[0]).Num(), 36);
    TestEqual(TEXT("High creates real sparks count"), ReadSeeds(Components[2]).Num(), 256);
    Extension->SetQuality(EParadoxAccretionQuality::Low);
    TestTrue(TEXT("Returning to Low preserves field"), ReadSeeds(Components[0]) == Initial[0]);
    Source->Destroy();
    Scope.World->Tick(LEVELTICK_All, 1.f/60.f);
    TestFalse(TEXT("Source destruction stops extension"), Extension->bActive);
    TestTrue(TEXT("Failure remains observable"), Extension->StateMessage.Contains(TEXT("removed")));
    Extension->Destroy();
    for (UNiagaraComponent* Component : Components) { TestFalse(TEXT("Destruction leaves no component active"), Component->IsActive()); }
    return true;
}
#endif
