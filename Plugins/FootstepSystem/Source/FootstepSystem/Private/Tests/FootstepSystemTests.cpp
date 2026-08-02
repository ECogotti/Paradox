#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimNotify_Footstep.h"
#include "Components/BoxComponent.h"
#include "Components/FootstepComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/FootstepProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundWave.h"

#include <limits>

struct FFootstepComponentTestAccessor
{
	static void ConfigureSockets(
		UFootstepComponent& Component,
		const FName LeftSocket,
		const FName RightSocket,
		const FName DefaultSocket)
	{
		Component.LeftFootSocket = LeftSocket;
		Component.RightFootSocket = RightSocket;
		Component.DefaultFootSocket = DefaultSocket;
	}

	static FName ResolveSocketName(
		const UFootstepComponent& Component,
		const FFootstepRequest& Request)
	{
		return Component.ResolveSocketName(Request);
	}

	static USkeletalMeshComponent* ResolveMesh(
		UFootstepComponent& Component,
		USkeletalMeshComponent* AnimationSourceMesh)
	{
		return Component.ResolveSkeletalMesh(AnimationSourceMesh);
	}

	static void AllowRequestAndSuppressSocketWarning(
		UFootstepComponent& Component,
		const FName SocketName)
	{
		Component.bAcceptingRequests = true;
		Component.ReportedMissingSockets.Add(SocketName);
	}

	static FFootstepEvent BuildEvent(
		const UFootstepComponent& Component,
		const FFootstepRequest& Request,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const FHitResult& Hit,
		const bool bHadValidHit)
	{
		return Component.BuildFootstepEvent(
			Request,
			TraceStart,
			TraceEnd,
			Hit,
			bHadValidHit);
	}

	static void ConfigureTrace(UFootstepComponent& Component)
	{
		Component.TraceShape = EFootstepTraceShape::Line;
		Component.TraceChannel = ECC_Visibility;
		Component.bTraceComplex = false;
		Component.bIgnoreOwner = true;
	}

	static bool PerformTrace(
		const UFootstepComponent& Component,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		FHitResult& OutHit)
	{
		return Component.PerformFloorTrace(TraceStart, TraceEnd, OutHit);
	}

	static void SuppressMissingProfileWarning(UFootstepComponent& Component)
	{
		Component.bReportedMissingProfile = true;
	}

	static bool ResolveResponse(
		UFootstepComponent& Component,
		FFootstepSurfaceResponse& OutResponse,
		bool& bOutUsedFallback)
	{
		return Component.ResolveConfiguredResponse(
			SurfaceType_Default,
			OutResponse,
			bOutUsedFallback);
	}

	static void SetBroadcastOnMiss(
		UFootstepComponent& Component,
		const bool bBroadcastOnMiss)
	{
		Component.bBroadcastOnNoFloorHit = bBroadcastOnMiss;
	}

	static bool FinalizeEvent(
		UFootstepComponent& Component,
		const FFootstepEvent& Event)
	{
		return Component.FinalizeFootstepEvent(Event, nullptr, false);
	}

	static void ConfigureDebug(
		UFootstepComponent& Component,
		const bool bEnabled,
		const int32 Categories)
	{
		Component.bEnableDebug = bEnabled;
		Component.DebugCategories = Categories;
	}

	static bool HasDebugCategory(
		const UFootstepComponent& Component,
		const EFootstepDebugCategory Category)
	{
		return Component.HasDebugCategory(Category);
	}

	static void EnableAllFeedback(UFootstepComponent& Component)
	{
		Component.bEnableAudio = true;
		Component.bEnableNiagara = true;
		Component.bEnableDecals = true;
	}

	static bool ShouldSpawnAudio(
		const UFootstepComponent& Component,
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response)
	{
		return Component.ShouldSpawnAudioFeedback(Event, Response);
	}

	static bool ShouldSpawnNiagara(
		const UFootstepComponent& Component,
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response)
	{
		return Component.ShouldSpawnNiagaraFeedback(Event, Response);
	}

	static bool ShouldSpawnDecal(
		const UFootstepComponent& Component,
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response)
	{
		return Component.ShouldSpawnDecalFeedback(Event, Response);
	}
};

namespace UE::FootstepSystem::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (!World)
			{
				return;
			}

			World->DestroyWorld(true);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	UFootstepComponent* AddFootstepComponent(AActor& Owner)
	{
		UFootstepComponent* Component =
			NewObject<UFootstepComponent>(&Owner, TEXT("FootstepComponent"));
		Owner.AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFootstepProfileAndIntensityTest,
	"FootstepSystem.Request.ProfileSelectionAndIntensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFootstepProfileAndIntensityTest::RunTest(const FString& Parameters)
{
	FFootstepRequest Request;

	Request.NormalizedIntensity = -0.25f;
	TestEqual(
		TEXT("Negative intensity clamps to zero"),
		Request.GetSanitized().NormalizedIntensity,
		0.0f);

	Request.NormalizedIntensity = 1.25f;
	TestEqual(
		TEXT("Intensity above one clamps to one"),
		Request.GetSanitized().NormalizedIntensity,
		1.0f);

	Request.NormalizedIntensity = std::numeric_limits<float>::quiet_NaN();
	TestEqual(
		TEXT("NaN intensity normalizes to one"),
		Request.GetSanitized().NormalizedIntensity,
		1.0f);

	Request.NormalizedIntensity = std::numeric_limits<float>::infinity();
	TestEqual(
		TEXT("Infinite intensity normalizes to one"),
		Request.GetSanitized().NormalizedIntensity,
		1.0f);

	UFootstepProfile* Profile = NewObject<UFootstepProfile>();
	FFootstepSurfaceResponse ExactResponse;
	ExactResponse.VolumeMultiplier = 0.35f;
	Profile->SurfaceResponses.Add(
		TEnumAsByte<EPhysicalSurface>(SurfaceType1),
		ExactResponse);

	FFootstepSurfaceResponse ResolvedResponse;
	bool bUsedFallback = true;
	TestTrue(
		TEXT("Exact surface response resolves"),
		Profile->ResolveResponse(
			SurfaceType1,
			ResolvedResponse,
			bUsedFallback));
	TestFalse(TEXT("Exact response is not fallback"), bUsedFallback);
	TestEqual(
		TEXT("Exact response value is copied"),
		ResolvedResponse.VolumeMultiplier,
		0.35f);

	Profile->bUseDefaultResponse = true;
	Profile->DefaultResponse.VolumeMultiplier = 0.75f;
	TestTrue(
		TEXT("Fallback response resolves"),
		Profile->ResolveResponse(
			SurfaceType2,
			ResolvedResponse,
			bUsedFallback));
	TestTrue(TEXT("Fallback use is reported"), bUsedFallback);
	TestEqual(
		TEXT("Fallback response value is copied"),
		ResolvedResponse.VolumeMultiplier,
		0.75f);

	Profile->bUseDefaultResponse = false;
	TestFalse(
		TEXT("Missing response without fallback does not resolve"),
		Profile->ResolveResponse(
			SurfaceType2,
			ResolvedResponse,
			bUsedFallback));
	TestFalse(TEXT("Missing response does not report fallback"), bUsedFallback);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFootstepSocketAndEventTest,
	"FootstepSystem.Component.SocketAndEventConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFootstepSocketAndEventTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UFootstepComponent* Component =
		NewObject<UFootstepComponent>(Owner, TEXT("FootstepComponent"));
	FFootstepComponentTestAccessor::ConfigureSockets(
		*Component,
		TEXT("foot_l"),
		TEXT("foot_r"),
		TEXT("foot_default"));

	FFootstepRequest Request;
	Request.Foot = EFootstepFoot::Left;
	Request.SocketOverride = TEXT("foot_override");
	TestEqual(
		TEXT("Request override has highest precedence"),
		FFootstepComponentTestAccessor::ResolveSocketName(*Component, Request),
		FName(TEXT("foot_override")));

	Request.SocketOverride = NAME_None;
	TestEqual(
		TEXT("Left foot selects left socket"),
		FFootstepComponentTestAccessor::ResolveSocketName(*Component, Request),
		FName(TEXT("foot_l")));
	Request.Foot = EFootstepFoot::Right;
	TestEqual(
		TEXT("Right foot selects right socket"),
		FFootstepComponentTestAccessor::ResolveSocketName(*Component, Request),
		FName(TEXT("foot_r")));
	Request.Foot = EFootstepFoot::Unspecified;
	TestEqual(
		TEXT("Unspecified foot selects default socket"),
		FFootstepComponentTestAccessor::ResolveSocketName(*Component, Request),
		FName(TEXT("foot_default")));

	FFootstepComponentTestAccessor::ConfigureSockets(
		*Component,
		NAME_None,
		NAME_None,
		NAME_None);
	TestTrue(
		TEXT("Missing configured socket remains missing"),
		FFootstepComponentTestAccessor::ResolveSocketName(*Component, Request).IsNone());

	AActor* HitActor = NewObject<AActor>();
	UBoxComponent* HitComponent =
		NewObject<UBoxComponent>(HitActor, TEXT("HitComponent"));
	UPhysicalMaterial* PhysicalMaterial = NewObject<UPhysicalMaterial>();
	PhysicalMaterial->SurfaceType = SurfaceType2;

	const FVector TraceStart(5.0f, 10.0f, 30.0f);
	const FVector TraceEnd(5.0f, 10.0f, -20.0f);
	const FVector ImpactPoint(5.0f, 10.0f, 0.0f);
	const FVector ImpactNormal(0.0f, 0.0f, 1.0f);
	FHitResult Hit(HitActor, HitComponent, ImpactPoint, ImpactNormal);
	Hit.PhysMaterial = PhysicalMaterial;

	Request.Foot = EFootstepFoot::Right;
	Request.NormalizedIntensity = 0.6f;
	const FFootstepEvent Event =
		FFootstepComponentTestAccessor::BuildEvent(
			*Component,
			Request,
			TraceStart,
			TraceEnd,
			Hit,
			true);
	TestTrue(TEXT("Synthetic hit is marked valid"), Event.bHadValidFloorHit);
	TestEqual(TEXT("Event snapshots source actor"), Event.InstigatorActor.Get(), Owner);
	TestEqual(TEXT("Event snapshots hit actor"), Event.HitActor.Get(), HitActor);
	TestEqual(
		TEXT("Event snapshots hit component"),
		Event.HitComponent.Get(),
		static_cast<UPrimitiveComponent*>(HitComponent));
	TestEqual(
		TEXT("Event snapshots physical material"),
		Event.PhysicalMaterial.Get(),
		PhysicalMaterial);
	TestEqual(
		TEXT("Event resolves physical surface"),
		Event.SurfaceType.GetValue(),
		SurfaceType2);
	TestEqual(TEXT("Event snapshots contact point"), Event.WorldLocation, ImpactPoint);
	TestEqual(TEXT("Event snapshots contact normal"), Event.SurfaceNormal, ImpactNormal);
	TestEqual(TEXT("Event snapshots trace start"), Event.TraceStart, TraceStart);
	TestEqual(TEXT("Event snapshots trace end"), Event.TraceEnd, TraceEnd);
	TestEqual(TEXT("Event snapshots intensity"), Event.NormalizedIntensity, 0.6f);

	Hit.PhysMaterial.Reset();
	const FFootstepEvent EventWithoutMaterial =
		FFootstepComponentTestAccessor::BuildEvent(
			*Component,
			Request,
			TraceStart,
			TraceEnd,
			Hit,
			true);
	TestNull(
		TEXT("Valid hit may omit physical material"),
		EventWithoutMaterial.PhysicalMaterial.Get());
	TestEqual(
		TEXT("Missing physical material maps to default surface"),
		EventWithoutMaterial.SurfaceType.GetValue(),
		SurfaceType_Default);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFootstepLifecycleDebugAndFeedbackTest,
	"FootstepSystem.Component.LifecycleDebugAndFeedbackGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFootstepLifecycleDebugAndFeedbackTest::RunTest(const FString& Parameters)
{
	AActor* Owner = NewObject<AActor>();
	UFootstepComponent* Component =
		NewObject<UFootstepComponent>(Owner, TEXT("FootstepComponent"));

	TestFalse(
		TEXT("Footstep component never ticks"),
		Component->PrimaryComponentTick.bCanEverTick);
	TestFalse(
		TEXT("Inactive component rejects direct submissions"),
		Component->SubmitFootstepRequest(FFootstepRequest()));

	FFootstepSurfaceResponse MissingResponse;
	bool bUsedFallback = true;
	FFootstepComponentTestAccessor::SuppressMissingProfileWarning(*Component);
	TestFalse(
		TEXT("Missing profile does not fabricate a response"),
		FFootstepComponentTestAccessor::ResolveResponse(
			*Component,
			MissingResponse,
			bUsedFallback));
	TestFalse(TEXT("Missing profile does not report fallback"), bUsedFallback);

	int32 NativeBroadcastCount = 0;
	Component->OnFootstepGeneratedNative().AddLambda(
		[&NativeBroadcastCount](const FFootstepEvent& Event)
		{
			++NativeBroadcastCount;
		});

	FFootstepEvent MissEvent;
	MissEvent.bHadValidFloorHit = false;
	FFootstepComponentTestAccessor::SetBroadcastOnMiss(*Component, false);
	TestFalse(
		TEXT("Miss is silent by default"),
		FFootstepComponentTestAccessor::FinalizeEvent(*Component, MissEvent));
	TestEqual(TEXT("Silent miss broadcasts nothing"), NativeBroadcastCount, 0);

	FFootstepComponentTestAccessor::SetBroadcastOnMiss(*Component, true);
	TestTrue(
		TEXT("Configured miss produces a public event"),
		FFootstepComponentTestAccessor::FinalizeEvent(*Component, MissEvent));
	TestEqual(TEXT("Configured miss broadcasts exactly once"), NativeBroadcastCount, 1);

	IConsoleVariable* DebugVariable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("FootstepSystem.Debug"));
	if (!TestNotNull(TEXT("Global debug CVar exists"), DebugVariable))
	{
		return false;
	}

	const int32 OriginalDebugValue = DebugVariable->GetInt();
	FFootstepComponentTestAccessor::ConfigureDebug(
		*Component,
		true,
		GetFootstepDebugCategoryMask(EFootstepDebugCategory::Trace));
	DebugVariable->Set(0, ECVF_SetByCode);
	TestFalse(
		TEXT("Global debug off overrides local debug"),
		Component->IsDebugEnabled());
	DebugVariable->Set(1, ECVF_SetByCode);
	TestTrue(
		TEXT("Global and local debug enable diagnostics"),
		Component->IsDebugEnabled());
	TestTrue(
		TEXT("Selected debug category is enabled"),
		FFootstepComponentTestAccessor::HasDebugCategory(
			*Component,
			EFootstepDebugCategory::Trace));
	TestFalse(
		TEXT("Unselected debug category remains disabled"),
		FFootstepComponentTestAccessor::HasDebugCategory(
			*Component,
			EFootstepDebugCategory::Audio));
	DebugVariable->Set(OriginalDebugValue, ECVF_SetByCode);

	FFootstepComponentTestAccessor::EnableAllFeedback(*Component);
	FFootstepSurfaceResponse Feedback;
	Feedback.Sound = NewObject<USoundWave>();
	Feedback.NiagaraSystem = NewObject<UNiagaraSystem>();
	Feedback.DecalMaterial = NewObject<UMaterial>();
	Feedback.bSpawnAudio = true;
	Feedback.bSpawnNiagara = true;
	Feedback.bSpawnDecal = true;
	Feedback.VolumeMultiplier = 1.0f;
	Feedback.NiagaraScale = 1.0f;
	Feedback.DecalSize = FVector(1.0f);

	FFootstepEvent FeedbackEvent;
	FeedbackEvent.NormalizedIntensity = 1.0f;
	TestTrue(
		TEXT("Valid audio configuration passes its spawn gate"),
		FFootstepComponentTestAccessor::ShouldSpawnAudio(
			*Component,
			FeedbackEvent,
			Feedback));
	TestTrue(
		TEXT("Valid Niagara configuration passes its spawn gate"),
		FFootstepComponentTestAccessor::ShouldSpawnNiagara(
			*Component,
			FeedbackEvent,
			Feedback));
	TestTrue(
		TEXT("Valid decal configuration passes its spawn gate"),
		FFootstepComponentTestAccessor::ShouldSpawnDecal(
			*Component,
			FeedbackEvent,
			Feedback));

	FeedbackEvent.NormalizedIntensity = 0.0f;
	TestFalse(
		TEXT("Zero intensity blocks audio feedback"),
		FFootstepComponentTestAccessor::ShouldSpawnAudio(
			*Component,
			FeedbackEvent,
			Feedback));
	TestFalse(
		TEXT("Zero intensity blocks Niagara feedback"),
		FFootstepComponentTestAccessor::ShouldSpawnNiagara(
			*Component,
			FeedbackEvent,
			Feedback));
	TestFalse(
		TEXT("Zero intensity blocks decal feedback"),
		FFootstepComponentTestAccessor::ShouldSpawnDecal(
			*Component,
			FeedbackEvent,
			Feedback));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFootstepTransientTraceAndNotifyTest,
	"FootstepSystem.Trace.TransientWorldAndNotifySafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFootstepTransientTraceAndNotifyTest::RunTest(const FString& Parameters)
{
	using namespace UE::FootstepSystem::Tests;

	FScopedTestWorld TestWorld(TEXT("FootstepSystemTraceWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* Owner = TestWorld.World->SpawnActor<AActor>();
	UFootstepComponent* Component = Owner
		? AddFootstepComponent(*Owner)
		: nullptr;
	if (!TestNotNull(TEXT("Footstep owner exists"), Owner)
		|| !TestNotNull(TEXT("Footstep component exists"), Component))
	{
		return false;
	}

	USkeletalMeshComponent* OwnerMesh =
		NewObject<USkeletalMeshComponent>(Owner, TEXT("OwnerMesh"));
	Owner->AddInstanceComponent(OwnerMesh);
	OwnerMesh->RegisterComponent();
	USkeletalMeshComponent* AnimationMesh =
		NewObject<USkeletalMeshComponent>(Owner, TEXT("AnimationMesh"));
	Owner->AddInstanceComponent(AnimationMesh);
	AnimationMesh->RegisterComponent();
	TestEqual(
		TEXT("Direct resolution caches the owner's first skeletal mesh"),
		FFootstepComponentTestAccessor::ResolveMesh(*Component, nullptr),
		OwnerMesh);
	TestEqual(
		TEXT("Animation resolution uses the mesh that fired the notify"),
		FFootstepComponentTestAccessor::ResolveMesh(*Component, AnimationMesh),
		AnimationMesh);
	TestEqual(
		TEXT("Animation resolution does not replace the direct-call mesh cache"),
		FFootstepComponentTestAccessor::ResolveMesh(*Component, nullptr),
		OwnerMesh);

	const FName NonexistentSocket(TEXT("socket_that_does_not_exist"));
	FFootstepComponentTestAccessor::ConfigureSockets(
		*Component,
		NonexistentSocket,
		NAME_None,
		NAME_None);
	FFootstepComponentTestAccessor::AllowRequestAndSuppressSocketWarning(
		*Component,
		NonexistentSocket);
	Component->Activate(true);
	FFootstepRequest MissingSocketRequest;
	MissingSocketRequest.Foot = EFootstepFoot::Left;
	TestFalse(
		TEXT("Existing socket name that is absent from the mesh rejects the request"),
		Component->SubmitFootstepRequestFromAnimation(
			MissingSocketRequest,
			AnimationMesh));

	AActor* FloorActor = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Floor actor exists"), FloorActor))
	{
		return false;
	}

	UPhysicalMaterial* FloorMaterial =
		NewObject<UPhysicalMaterial>(FloorActor, TEXT("FloorMaterial"));
	FloorMaterial->SurfaceType = SurfaceType2;

	UBoxComponent* FloorComponent =
		NewObject<UBoxComponent>(FloorActor, TEXT("FloorCollision"));
	FloorActor->AddInstanceComponent(FloorComponent);
	FloorActor->SetRootComponent(FloorComponent);
	FloorComponent->SetBoxExtent(FVector(100.0f, 100.0f, 10.0f));
	FloorComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FloorComponent->SetCollisionObjectType(ECC_WorldStatic);
	FloorComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	FloorComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	FloorComponent->SetPhysMaterialOverride(FloorMaterial);
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
	FloorComponent->RegisterComponent();

	FFootstepComponentTestAccessor::ConfigureTrace(*Component);
	FHitResult Hit;
	TestTrue(
		TEXT("Real line trace reaches transient floor"),
		FFootstepComponentTestAccessor::PerformTrace(
			*Component,
			FVector(0.0f, 0.0f, 100.0f),
			FVector(0.0f, 0.0f, -100.0f),
			Hit));
	TestEqual(
		TEXT("Real trace reports floor actor"),
		Hit.GetActor(),
		FloorActor);
	TestEqual(
		TEXT("Real trace reports floor component"),
		Hit.GetComponent(),
		static_cast<UPrimitiveComponent*>(FloorComponent));

	FFootstepRequest TraceRequest;
	TraceRequest.Foot = EFootstepFoot::Unspecified;
	const FFootstepEvent TraceEvent =
		FFootstepComponentTestAccessor::BuildEvent(
			*Component,
			TraceRequest,
			FVector(0.0f, 0.0f, 100.0f),
			FVector(0.0f, 0.0f, -100.0f),
			Hit,
			true);
	TestEqual(
		TEXT("Event snapshots the real trace physical material"),
		TraceEvent.PhysicalMaterial.Get(),
		Hit.PhysMaterial.Get());
	TestEqual(
		TEXT("Event resolves the real trace physical surface"),
		TraceEvent.SurfaceType.GetValue(),
		UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get()));

	IConsoleVariable* DebugVariable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("FootstepSystem.Debug"));
	const int32 OriginalDebugValue = DebugVariable ? DebugVariable->GetInt() : 0;
	if (DebugVariable)
	{
		DebugVariable->Set(0, ECVF_SetByCode);
	}

	AActor* NotifyOwner = TestWorld.World->SpawnActor<AActor>();
	USkeletalMeshComponent* NotifyMesh =
		NewObject<USkeletalMeshComponent>(NotifyOwner, TEXT("NotifyMesh"));
	NotifyOwner->AddInstanceComponent(NotifyMesh);
	NotifyOwner->SetRootComponent(NotifyMesh);
	NotifyMesh->RegisterComponent();

	UAnimNotify_Footstep* Notify = NewObject<UAnimNotify_Footstep>();
	FAnimNotifyEventReference EventReference;
	Notify->Notify(NotifyMesh, nullptr, EventReference);
	TestTrue(
		TEXT("Notify without a FootstepComponent fails safely"),
		IsValid(NotifyOwner) && IsValid(NotifyMesh));

	if (DebugVariable)
	{
		DebugVariable->Set(OriginalDebugValue, ECVF_SetByCode);
	}

	return true;
}

#endif
