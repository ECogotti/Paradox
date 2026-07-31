#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimNotify_Footstep.h"
#include "Animation/AnimSequence.h"
#include "Actions/ParadoxSetCrouchedActionDefinition.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/FootstepComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Data/FootstepProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "Footsteps/ParadoxFootstepNoiseComponent.h"
#include "Footsteps/ParadoxFootstepNoiseProfile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Paradox.h"
#include "UObject/UnrealType.h"

struct FParadoxFootstepNoiseTestAccessor
{
	static void Configure(
		UParadoxFootstepNoiseComponent& Component,
		UParadoxFootstepNoiseProfile* Profile,
		UPerceptionKnowledgeSourceComponent* Source)
	{
		Component.NoiseProfile = Profile;
		Component.PerceptionSource = Source;
		Component.bAcceptingEvents = true;
	}

	static void SetIgnoreCrouch(
		UParadoxFootstepNoiseComponent& Component,
		const bool bIgnore)
	{
		Component.bIgnoreNoiseDuringCrouch = bIgnore;
	}

	static void SetDebug(
		UParadoxFootstepNoiseComponent& Component,
		const bool bEnabled)
	{
		Component.bEnableDebug = bEnabled;
	}

	static void HandleEvent(
		UParadoxFootstepNoiseComponent& Component,
		const FFootstepEvent& Event)
	{
		Component.HandleFootstepGenerated(Event);
	}

	static EParadoxFootstepNoiseResult ProcessEvent(
		UParadoxFootstepNoiseComponent& Component,
		const FFootstepEvent& Event)
	{
		FParadoxFootstepNoiseResponse Response;
		bool bOwnerCrouched = false;
		float EffectiveLoudness = 0.0f;
		FString Diagnostic;
		return Component.ProcessFootstepEvent(
			Event,
			Response,
			bOwnerCrouched,
			EffectiveLoudness,
			Diagnostic);
	}

	static void SetEmitter(
		UParadoxFootstepNoiseComponent& Component,
		TFunction<
			FPerceptionKnowledgeOperationResult(
				UPerceptionKnowledgeSourceComponent&,
				const FPerceptionKnowledgeNoiseRequest&)> Emitter)
	{
		Component.TestNoiseEmitter = MoveTemp(Emitter);
	}

	static UFootstepComponent* GetBoundFootstepComponent(
		const UParadoxFootstepNoiseComponent& Component)
	{
		return Component.BoundFootstepComponent.Get();
	}
};

namespace UE::Paradox::Footsteps::Tests
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

		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	FFootstepEvent MakeValidEvent(
		AActor& Owner,
		const EPhysicalSurface Surface = SurfaceType1,
		const float Intensity = 1.0f)
	{
		FFootstepEvent Event;
		Event.InstigatorActor = &Owner;
		Event.SurfaceType = Surface;
		Event.WorldLocation = FVector(125.0f, -45.0f, 10.0f);
		Event.SurfaceNormal = FVector::UpVector;
		Event.NormalizedIntensity = Intensity;
		Event.bHadValidFloorHit = true;
		return Event;
	}

	FPerceptionKnowledgeOperationResult MakeEmissionResult(
		const EPerceptionKnowledgeOperationStatus Status,
		const TCHAR* Message)
	{
		FPerceptionKnowledgeOperationResult Result;
		Result.Status = Status;
		Result.Message = Message;
		return Result;
	}

	UParadoxFootstepNoiseProfile* MakeFallbackProfile(UObject& Outer)
	{
		UParadoxFootstepNoiseProfile* Profile =
			NewObject<UParadoxFootstepNoiseProfile>(&Outer);
		Profile->bUseDefaultResponse = true;
		Profile->DefaultResponse.EventTag =
			ParadoxGameplayTags::Event_Noise_Character_Footstep.GetTag();
		Profile->DefaultResponse.CauseTag =
			ParadoxGameplayTags::Cause_CharacterMovement_Footstep.GetTag();
		Profile->DefaultResponse.BaseLoudness = 1.0f;
		Profile->DefaultResponse.MaxRange = 0.0f;
		Profile->DefaultResponse.bEmitNoise = true;
		return Profile;
	}

	template<typename TCharacter>
	TCharacter* SpawnCharacter(UWorld& World)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<TCharacter>(
			TCharacter::StaticClass(),
			FTransform::Identity,
			Parameters);
	}

	bool ValidateConfiguredCharacter(
		FAutomationTestBase& Test,
		const TCHAR* ClassPath,
		const TCHAR* Role)
	{
		UClass* CharacterClass = LoadClass<AParadoxCharacter>(
			nullptr,
			ClassPath);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s Blueprint class loads"), Role),
			CharacterClass))
		{
			return false;
		}

		const AParadoxCharacter* Character =
			Cast<AParadoxCharacter>(CharacterClass->GetDefaultObject());
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s CDO exists"), Role),
			Character))
		{
			return false;
		}

		UFootstepComponent* Footstep = Character->GetFootstepComponent();
		UParadoxFootstepNoiseComponent* Adapter =
			Character->GetFootstepNoiseComponent();
		UPerceptionKnowledgeSourceComponent* Source =
			Character->GetPerceptionKnowledgeSourceComponent();
		Test.TestNotNull(
			*FString::Printf(TEXT("%s has Footstep component"), Role),
			Footstep);
		Test.TestNotNull(
			*FString::Printf(TEXT("%s has noise adapter"), Role),
			Adapter);
		Test.TestNotNull(
			*FString::Printf(TEXT("%s has semantic Source"), Role),
			Source);
		if (!Footstep || !Adapter || !Source)
		{
			return false;
		}

		const FObjectProperty* FootstepProfileProperty =
			FindFProperty<FObjectProperty>(
				UFootstepComponent::StaticClass(),
				TEXT("FootstepProfile"));
		const FNameProperty* LeftSocketProperty =
			FindFProperty<FNameProperty>(
				UFootstepComponent::StaticClass(),
				TEXT("LeftFootSocket"));
		const FNameProperty* RightSocketProperty =
			FindFProperty<FNameProperty>(
				UFootstepComponent::StaticClass(),
				TEXT("RightFootSocket"));
		const FBoolProperty* RegisterSightProperty =
			FindFProperty<FBoolProperty>(
				UPerceptionKnowledgeSourceComponent::StaticClass(),
				TEXT("bRegisterForSight"));
		const FBoolProperty* RegisterHearingProperty =
			FindFProperty<FBoolProperty>(
				UPerceptionKnowledgeSourceComponent::StaticClass(),
				TEXT("bRegisterForHearing"));
		if (!FootstepProfileProperty
			|| !LeftSocketProperty
			|| !RightSocketProperty
			|| !RegisterSightProperty
			|| !RegisterHearingProperty)
		{
			Test.AddError(
				TEXT("Expected reflected FootstepSystem or PerceptionKnowledge properties were not found."));
			return false;
		}

		const UObject* CosmeticProfile =
			FootstepProfileProperty->GetObjectPropertyValue_InContainer(
				Footstep);
		Test.TestEqual(
			*FString::Printf(TEXT("%s cosmetic profile"), Role),
			GetPathNameSafe(CosmeticProfile),
			FString(TEXT("/Game/Characters/Astronaut/DataAssets/Footsteps/DA_AstronautFootsteps.DA_AstronautFootsteps")));
		Test.TestEqual(
			*FString::Printf(TEXT("%s left foot socket"), Role),
			LeftSocketProperty->GetPropertyValue_InContainer(Footstep),
			FName(TEXT("LeftFoot")));
		Test.TestEqual(
			*FString::Printf(TEXT("%s right foot socket"), Role),
			RightSocketProperty->GetPropertyValue_InContainer(Footstep),
			FName(TEXT("RightFoot")));
		Test.TestEqual(
			*FString::Printf(TEXT("%s AI noise profile"), Role),
			GetPathNameSafe(Adapter->GetNoiseProfile()),
			FString(TEXT("/Game/Characters/Astronaut/DataAssets/Footsteps/DA_AstronautFootstepNoise.DA_AstronautFootstepNoise")));
		Test.TestFalse(
			*FString::Printf(TEXT("%s Source disables Sight"), Role),
			RegisterSightProperty->GetPropertyValue_InContainer(Source));
		Test.TestTrue(
			*FString::Printf(TEXT("%s Source enables Hearing"), Role),
			RegisterHearingProperty->GetPropertyValue_InContainer(Source));
		Test.TestTrue(
			*FString::Printf(TEXT("%s movement can crouch"), Role),
			Character->GetCharacterMovement()
				->GetNavAgentPropertiesRef()
				.bCanCrouch);
		return true;
	}

	bool HasFootstepNotify(
		const UAnimSequence& Sequence,
		const EFootstepFoot Foot)
	{
		for (const FAnimNotifyEvent& Event : Sequence.Notifies)
		{
			const UAnimNotify_Footstep* Notify =
				Cast<UAnimNotify_Footstep>(Event.Notify);
			if (Notify
				&& Notify->Foot == Foot
				&& Notify->SocketOverride.IsNone()
				&& FMath::IsNearlyEqual(
					Notify->NormalizedIntensity,
					1.0f))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxFootstepProfileTest,
	"Paradox.Footsteps.Profile.SurfaceFallbackAndDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxFootstepProfileTest::RunTest(const FString& Parameters)
{
	UParadoxFootstepNoiseProfile* Profile =
		NewObject<UParadoxFootstepNoiseProfile>();
	FParadoxFootstepNoiseResponse Exact;
	Exact.BaseLoudness = 0.35f;
	Profile->SurfaceResponses.Add(SurfaceType1, Exact);
	Profile->bUseDefaultResponse = true;
	Profile->DefaultResponse.BaseLoudness = 0.8f;

	FParadoxFootstepNoiseResponse Resolved;
	bool bUsedFallback = true;
	TestTrue(
		TEXT("Exact surface resolves"),
		Profile->ResolveResponse(SurfaceType1, Resolved, bUsedFallback));
	TestFalse(TEXT("Exact response is not fallback"), bUsedFallback);
	TestEqual(TEXT("Exact response value"), Resolved.BaseLoudness, 0.35f);

	TestTrue(
		TEXT("Missing surface resolves fallback"),
		Profile->ResolveResponse(SurfaceType2, Resolved, bUsedFallback));
	TestTrue(TEXT("Fallback is reported"), bUsedFallback);
	TestEqual(TEXT("Fallback response value"), Resolved.BaseLoudness, 0.8f);

	Profile->bUseDefaultResponse = false;
	TestFalse(
		TEXT("Unmapped surface without fallback fails"),
		Profile->ResolveResponse(SurfaceType2, Resolved, bUsedFallback));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxFootstepDecisionTest,
	"Paradox.Footsteps.Runtime.StandingCrouchFailuresAndPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxFootstepDecisionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Footsteps::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxFootstepDecisionWorld"));
	if (!TestNotNull(TEXT("Test world exists"), TestWorld.World))
	{
		return false;
	}
	AParadoxPlayerCharacter* Character =
		SpawnCharacter<AParadoxPlayerCharacter>(*TestWorld.World);
	if (!TestNotNull(TEXT("Player Character exists"), Character))
	{
		return false;
	}
	TestWorld.StartPlay();

	UParadoxFootstepNoiseComponent* Adapter =
		Character->GetFootstepNoiseComponent();
	UPerceptionKnowledgeSourceComponent* Source =
		Character->GetPerceptionKnowledgeSourceComponent();
	UParadoxFootstepNoiseProfile* Profile =
		MakeFallbackProfile(*Character);
	if (!TestNotNull(TEXT("Adapter exists"), Adapter)
		|| !TestNotNull(TEXT("Source exists"), Source))
	{
		return false;
	}
	FParadoxFootstepNoiseTestAccessor::Configure(
		*Adapter,
		Profile,
		Source);

	int32 EmissionCount = 0;
	FPerceptionKnowledgeNoiseRequest LastRequest;
	FParadoxFootstepNoiseTestAccessor::SetEmitter(
		*Adapter,
		[&EmissionCount, &LastRequest](
			UPerceptionKnowledgeSourceComponent&,
			const FPerceptionKnowledgeNoiseRequest& Request)
		{
			++EmissionCount;
			LastRequest = Request;
			return MakeEmissionResult(
				EPerceptionKnowledgeOperationStatus::Success,
				TEXT("Test emission succeeded."));
		});

	FFootstepEvent Event = MakeValidEvent(*Character, SurfaceType3, 0.4f);
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Standing footstep emits"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::Emitted);
	TestEqual(TEXT("Standing emission count"), EmissionCount, 1);
	TestEqual(TEXT("Event tag propagates"), LastRequest.EventTag, Profile->DefaultResponse.EventTag);
	TestEqual(TEXT("Cause tag propagates"), LastRequest.CauseTag, Profile->DefaultResponse.CauseTag);
	TestEqual(TEXT("Instigator propagates"), LastRequest.Instigator.Get(), static_cast<AActor*>(Character));
	TestEqual(TEXT("Contact location propagates"), LastRequest.WorldLocation, Event.WorldLocation);
	TestFalse(TEXT("Contact location overrides source location"), LastRequest.bUseSourceLocation);
	TestEqual(TEXT("Intensity scales loudness"), LastRequest.Loudness, 0.4f);
	TestEqual(TEXT("Loudness propagates as strength"), LastRequest.Strength, 0.4f);
	TestEqual(TEXT("Listener controls range"), LastRequest.MaxRange, 0.0f);

	Character->SetIsCrouched(true);
	FParadoxFootstepNoiseTestAccessor::SetIgnoreCrouch(*Adapter, true);
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Crouched footstep is suppressed"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::SuppressedByCrouch);
	TestEqual(TEXT("Suppression emits no noise"), EmissionCount, 1);

	FParadoxFootstepNoiseTestAccessor::SetIgnoreCrouch(*Adapter, false);
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Crouch suppression can be disabled"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::Emitted);
	TestEqual(TEXT("Disabled suppression emits"), EmissionCount, 2);

	Profile->DefaultResponse.bEmitNoise = false;
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Surface can disable AI noise"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::DisabledBySurface);
	TestEqual(TEXT("Disabled surface emits no noise"), EmissionCount, 2);
	Profile->DefaultResponse.bEmitNoise = true;

	FParadoxFootstepNoiseTestAccessor::Configure(*Adapter, nullptr, Source);
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Missing profile is classified"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::MissingNoiseProfile);

	UParadoxFootstepNoiseProfile* NoFallback =
		NewObject<UParadoxFootstepNoiseProfile>(Character);
	FParadoxFootstepNoiseTestAccessor::Configure(
		*Adapter,
		NoFallback,
		Source);
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Missing surface response is classified"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::MissingSurfaceResponse);

	FParadoxFootstepNoiseTestAccessor::Configure(
		*Adapter,
		Profile,
		Source);
	FFootstepEvent InvalidEvent = Event;
	InvalidEvent.bHadValidFloorHit = false;
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, InvalidEvent);
	TestEqual(
		TEXT("Invalid event is classified"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::InvalidEvent);

	FParadoxFootstepNoiseTestAccessor::SetEmitter(
		*Adapter,
		[](
			UPerceptionKnowledgeSourceComponent&,
			const FPerceptionKnowledgeNoiseRequest&)
		{
			return MakeEmissionResult(
				EPerceptionKnowledgeOperationStatus::InvalidArgument,
				TEXT("Synthetic rejection."));
		});
	FParadoxFootstepNoiseTestAccessor::HandleEvent(*Adapter, Event);
	TestEqual(
		TEXT("Perception failure is classified"),
		Adapter->GetLastResult(),
		EParadoxFootstepNoiseResult::EmissionFailed);

	UParadoxFootstepNoiseComponent* Ownerless =
		NewObject<UParadoxFootstepNoiseComponent>();
	TestEqual(
		TEXT("Ownerless adapter rejects events"),
		FParadoxFootstepNoiseTestAccessor::ProcessEvent(
			*Ownerless,
			Event),
		EParadoxFootstepNoiseResult::InvalidOwner);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxFootstepLifecycleAndDebugTest,
	"Paradox.Footsteps.Runtime.BindingTickAndDebugGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxFootstepLifecycleAndDebugTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Footsteps::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxFootstepLifecycleWorld"));
	if (!TestNotNull(TEXT("Test world exists"), TestWorld.World))
	{
		return false;
	}
	AParadoxPlayerCharacter* Character =
		SpawnCharacter<AParadoxPlayerCharacter>(*TestWorld.World);
	if (!TestNotNull(TEXT("Player Character exists"), Character))
	{
		return false;
	}
	TestWorld.StartPlay();
	if (!Character->HasActorBegunPlay())
	{
		// Transient automation worlds do not always dispatch BeginPlay to
		// actors spawned before InitializeActorsForPlay.
		Character->DispatchBeginPlay();
	}

	UFootstepComponent* Footstep = Character->GetFootstepComponent();
	UParadoxFootstepNoiseComponent* Adapter =
		Character->GetFootstepNoiseComponent();
	if (!TestNotNull(TEXT("Footstep component exists"), Footstep)
		|| !TestNotNull(TEXT("Adapter exists"), Adapter))
	{
		return false;
	}
	TestFalse(
		TEXT("Adapter never ticks"),
		Adapter->PrimaryComponentTick.bCanEverTick);
	TestTrue(TEXT("Adapter is registered"), Adapter->IsRegistered());
	TestTrue(TEXT("Adapter began play"), Adapter->HasBegunPlay());
	TestEqual(
		TEXT("Adapter resolves the owning Footstep component"),
		FParadoxFootstepNoiseTestAccessor::GetBoundFootstepComponent(
			*Adapter),
		Footstep);
	TestTrue(
		TEXT("Adapter binds the native footstep delegate"),
		Footstep->OnFootstepGeneratedNative().IsBoundToObject(Adapter));

	IConsoleVariable* DebugCVar =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("Paradox.Footsteps.Debug"));
	if (!TestNotNull(TEXT("Global debug CVar exists"), DebugCVar))
	{
		return false;
	}
	const int32 OriginalValue = DebugCVar->GetInt();
	FParadoxFootstepNoiseTestAccessor::SetDebug(*Adapter, true);
	DebugCVar->Set(0, ECVF_SetByCode);
	TestFalse(TEXT("Global off disables local debug"), Adapter->IsDebugEnabled());
	DebugCVar->Set(1, ECVF_SetByCode);
	TestTrue(TEXT("Global and local enable debug"), Adapter->IsDebugEnabled());
	FParadoxFootstepNoiseTestAccessor::SetDebug(*Adapter, false);
	TestFalse(TEXT("Local off disables global debug"), Adapter->IsDebugEnabled());
	DebugCVar->Set(OriginalValue, ECVF_SetByCode);

	Adapter->DestroyComponent();
	TestFalse(
		TEXT("Destroying adapter unbinds the native delegate"),
		Footstep->OnFootstepGeneratedNative().IsBoundToObject(Adapter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxFootstepAssetConfigurationTest,
	"Paradox.Footsteps.Assets.CharacterInputProfilesAndNotifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxFootstepAssetConfigurationTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Footsteps::Tests;

	ValidateConfiguredCharacter(
		*this,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerAstronaut.BP_PlayerAstronaut_C"),
		TEXT("Player"));
	ValidateConfiguredCharacter(
		*this,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_CloneAstronaut.BP_CloneAstronaut_C"),
		TEXT("Clone"));

	UFootstepProfile* CosmeticProfile = LoadObject<UFootstepProfile>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/DataAssets/Footsteps/DA_AstronautFootsteps.DA_AstronautFootsteps"));
	if (TestNotNull(TEXT("Cosmetic profile loads"), CosmeticProfile))
	{
		TestTrue(
			TEXT("Cosmetic profile uses one all-surface fallback"),
			CosmeticProfile->bUseDefaultResponse);
		TestEqual(
			TEXT("Cosmetic profile has no redundant surface entries"),
			CosmeticProfile->SurfaceResponses.Num(),
			0);
		TestTrue(
			TEXT("Cosmetic fallback keeps audio"),
			CosmeticProfile->DefaultResponse.bSpawnAudio
				&& CosmeticProfile->DefaultResponse.Sound != nullptr);
		TestTrue(
			TEXT("Cosmetic fallback keeps Niagara"),
			CosmeticProfile->DefaultResponse.bSpawnNiagara
				&& CosmeticProfile->DefaultResponse.NiagaraSystem != nullptr);
		TestFalse(
			TEXT("Cosmetic fallback keeps decals disabled"),
			CosmeticProfile->DefaultResponse.bSpawnDecal);
	}

	UParadoxFootstepNoiseProfile* NoiseProfile =
		LoadObject<UParadoxFootstepNoiseProfile>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/DataAssets/Footsteps/DA_AstronautFootstepNoise.DA_AstronautFootstepNoise"));
	if (TestNotNull(TEXT("AI noise profile loads"), NoiseProfile))
	{
		TestTrue(TEXT("AI profile uses fallback"), NoiseProfile->bUseDefaultResponse);
		TestEqual(TEXT("AI profile has no surface entries"), NoiseProfile->SurfaceResponses.Num(), 0);
		TestEqual(TEXT("AI profile base loudness"), NoiseProfile->DefaultResponse.BaseLoudness, 1.0f);
		TestEqual(TEXT("AI profile uses listener range"), NoiseProfile->DefaultResponse.MaxRange, 0.0f);
		TestTrue(TEXT("AI profile emits noise"), NoiseProfile->DefaultResponse.bEmitNoise);
		TestEqual(
			TEXT("AI profile event tag"),
			NoiseProfile->DefaultResponse.EventTag,
			ParadoxGameplayTags::Event_Noise_Character_Footstep.GetTag());
		TestEqual(
			TEXT("AI profile cause tag"),
			NoiseProfile->DefaultResponse.CauseTag,
			ParadoxGameplayTags::Cause_CharacterMovement_Footstep.GetTag());
	}

	UInputAction* CrouchAction = LoadObject<UInputAction>(
		nullptr,
		TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
	UInputMappingContext* MappingContext =
		LoadObject<UInputMappingContext>(
			nullptr,
			TEXT("/Game/Input/IMC_Default.IMC_Default"));
	TestNotNull(TEXT("Crouch Input Action loads"), CrouchAction);
	if (TestNotNull(TEXT("Default mapping context loads"), MappingContext)
		&& CrouchAction)
	{
		bool bFoundLeftControl = false;
		for (const FEnhancedActionKeyMapping& Mapping :
			MappingContext->GetMappings())
		{
			if (Mapping.Action == CrouchAction
				&& Mapping.Key == EKeys::LeftControl)
			{
				bFoundLeftControl = true;
				break;
			}
		}
		TestTrue(
			TEXT("Crouch action maps to Left Control"),
			bFoundLeftControl);
	}

	UClass* ControllerClass = LoadClass<AParadoxPlayerController>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
	UParadoxSetCrouchedActionDefinition* SetCrouchedDefinition =
		LoadObject<UParadoxSetCrouchedActionDefinition>(
			nullptr,
			TEXT("/Game/Data/GameplayActions/DA_ParadoxSetCrouched.DA_ParadoxSetCrouched"));
	if (TestNotNull(
		TEXT("Paradox Set Crouched Definition loads"),
		SetCrouchedDefinition))
	{
		TestTrue(
			TEXT("Set Crouched Definition uses the stance lock"),
			SetCrouchedDefinition->ExecutionLocks.HasTagExact(
				ParadoxGameplayTags::Lock_Stance));
		TestEqual(
			TEXT("Set Crouched Definition queues only on an exact lock conflict"),
			SetCrouchedDefinition->BlockedPolicy,
			EGameplayActionBlockedPolicy::Queue);
		TestEqual(
			TEXT("Set Crouched Definition has neutral priority"),
			SetCrouchedDefinition->DefaultPriority,
			0);
	}
	if (TestNotNull(TEXT("Player Controller Blueprint loads"), ControllerClass))
	{
		const AParadoxPlayerController* Controller =
			Cast<AParadoxPlayerController>(
				ControllerClass->GetDefaultObject());
		const FObjectProperty* CrouchActionProperty =
			FindFProperty<FObjectProperty>(
				AParadoxPlayerController::StaticClass(),
				TEXT("CrouchAction"));
		if (TestNotNull(
			TEXT("CrouchAction property exists"),
			CrouchActionProperty))
		{
			TestEqual(
				TEXT("Player Controller assigns IA_Crouch"),
				CrouchActionProperty->GetObjectPropertyValue_InContainer(
					Controller),
				static_cast<UObject*>(CrouchAction));
		}
		const FObjectProperty* SetCrouchedDefinitionProperty =
			FindFProperty<FObjectProperty>(
				AParadoxPlayerController::StaticClass(),
				TEXT("SetCrouchedActionDefinition"));
		if (TestNotNull(
			TEXT("SetCrouchedActionDefinition property exists"),
			SetCrouchedDefinitionProperty))
		{
			TestEqual(
				TEXT("Player Controller assigns the ready-to-use crouch Definition"),
				SetCrouchedDefinitionProperty->GetObjectPropertyValue_InContainer(
					Controller),
				static_cast<UObject*>(SetCrouchedDefinition));
		}
	}

	for (const TCHAR* AnimationPath : {
		TEXT("/Game/Characters/Astronaut/Animations/AS_astronaut_walk.AS_astronaut_walk"),
		TEXT("/Game/Characters/Astronaut/Animations/AS_astronaut_run.AS_astronaut_run") })
	{
		UAnimSequence* Sequence =
			LoadObject<UAnimSequence>(nullptr, AnimationPath);
		if (!TestNotNull(
			*FString::Printf(TEXT("Animation loads: %s"), AnimationPath),
			Sequence))
		{
			continue;
		}
		TestTrue(
			*FString::Printf(TEXT("%s has left footstep"), AnimationPath),
			HasFootstepNotify(*Sequence, EFootstepFoot::Left));
		TestTrue(
			*FString::Printf(TEXT("%s has right footstep"), AnimationPath),
			HasFootstepNotify(*Sequence, EFootstepFoot::Right));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
