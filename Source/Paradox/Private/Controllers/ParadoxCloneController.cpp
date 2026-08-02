#include "Controllers/ParadoxCloneController.h"

#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/PerceptionKnowledgeHearingRangeRendererComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Data/PerceptionKnowledgeProfile.h"
#include "Paradox.h"

AParadoxCloneController::AParadoxCloneController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PerceptionKnowledgeListener =
		CreateDefaultSubobject<UPerceptionKnowledgeListenerComponent>(
			TEXT("PerceptionKnowledgeListener"));
	HearingRangeRenderer =
		CreateDefaultSubobject<
			UPerceptionKnowledgeHearingRangeRendererComponent>(
			TEXT("PerceptionKnowledgeHearingRangeRenderer"));
	PerceptionProfile =
		CreateDefaultSubobject<UPerceptionKnowledgeProfile>(
			TEXT("DefaultPerceptionKnowledgeProfile"));
	PerceptionKnowledgeListener->SetListenerProfile(PerceptionProfile);
}

void AParadoxCloneController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (PerceptionKnowledgeListener)
	{
		UPerceptionKnowledgeProfile* ListenerProfile =
			PerceptionKnowledgeListener->GetListenerProfile();
		const bool bUsesControllerOwnedFallback =
			ListenerProfile && ListenerProfile->IsIn(this);
		UPerceptionKnowledgeProfile* DesiredProfile =
			(!ListenerProfile || bUsesControllerOwnedFallback)
				&& PerceptionProfile
				? PerceptionProfile.Get()
				: ListenerProfile;

		// A Profile authored directly on the Listener component is authoritative. The
		// controller-level property remains a compatible override only for an empty Listener or
		// the native controller-owned fallback created in this class's constructor. Reapply even
		// when the pointer is unchanged: a dynamically spawned Blueprint controller can register
		// the native sense configs before its component-template Profile has been serialized.
		if (DesiredProfile)
		{
			PerceptionKnowledgeListener->SetListenerProfile(
				DesiredProfile);
		}
	}
	AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(InPawn);
	UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
		Clone ? Clone->GetBehaviorCoordinator() : nullptr;
	if (Coordinator)
	{
		Coordinator->SetBehaviorTreeContext(nullptr, nullptr);
	}
}

bool AParadoxCloneController::StartCloneBehaviorTree(
	FString& OutDiagnostic)
{
	AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(GetPawn());
	UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
		Clone ? Clone->GetBehaviorCoordinator() : nullptr;
	if (!CloneBehaviorTree)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Clone controller '%s' has no CloneBehaviorTree. Clone '%s' remains stationary; author the documented BT/Blackboard assets."),
			*GetNameSafe(this),
			*GetNameSafe(Clone));
		PARADOX_LOG_ERROR(TEXT("%s"), *OutDiagnostic);
		return false;
	}
	if (!Clone || !Coordinator)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Clone controller '%s' cannot start behavior because the possessed Paradox clone or coordinator is missing."),
			*GetNameSafe(this));
		PARADOX_LOG_ERROR(TEXT("%s"), *OutDiagnostic);
		return false;
	}
	if (!RunBehaviorTree(CloneBehaviorTree))
	{
		OutDiagnostic = FString::Printf(
			TEXT("RunBehaviorTree rejected '%s'."),
			*GetNameSafe(CloneBehaviorTree));
		return false;
	}
	UBehaviorTreeComponent* TreeComponent =
		Cast<UBehaviorTreeComponent>(GetBrainComponent());
	if (!ValidateBlackboardContract(OutDiagnostic))
	{
		if (TreeComponent)
		{
			TreeComponent->StopTree(EBTStopMode::Safe);
		}
		Coordinator->SetBehaviorTreeContext(nullptr, nullptr);
		return false;
	}
	Coordinator->SetBehaviorTreeContext(
		TreeComponent,
		GetBlackboardComponent());
	OutDiagnostic.Reset();
	return true;
}

void AParadoxCloneController::OnUnPossess()
{
	if (AParadoxCloneCharacter* Clone =
		Cast<AParadoxCloneCharacter>(GetPawn()))
	{
		if (UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
			Clone->GetBehaviorCoordinator())
		{
			Coordinator->SetBehaviorTreeContext(nullptr, nullptr);
		}
	}
	Super::OnUnPossess();
}

bool AParadoxCloneController::ValidateBlackboardContract(
	FString& OutDiagnostic) const
{
	const UBlackboardComponent* BlackboardComp = GetBlackboardComponent();
	const UBlackboardData* Data = BlackboardComp
		? BlackboardComp->GetBlackboardAsset()
		: nullptr;
	if (!Data)
	{
		OutDiagnostic = TEXT("No initialized Blackboard asset.");
		return false;
	}

	struct FRequiredKey
	{
		FName Name;
		UClass* Type;
		UEnum* ExpectedEnum = nullptr;
		UClass* ExpectedObjectBase = nullptr;
	};
	const TArray<FRequiredKey> Required = {
		{ ParadoxCloneBlackboardKeys::BehaviorMode, UBlackboardKeyType_Enum::StaticClass(), StaticEnum<EParadoxCloneBehaviorMode>() },
		{ ParadoxCloneBlackboardKeys::InvestigationLocation, UBlackboardKeyType_Vector::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationSourceActor, UBlackboardKeyType_Object::StaticClass(), nullptr, AActor::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationSourceEntityId, UBlackboardKeyType_String::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationJournalEntryId, UBlackboardKeyType_String::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationObservationType, UBlackboardKeyType_Enum::StaticClass(), StaticEnum<EPerceptionKnowledgeObservationType>() },
		{ ParadoxCloneBlackboardKeys::InvestigationSemanticTag, UBlackboardKeyType_Name::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationSense, UBlackboardKeyType_Name::StaticClass() },
		{ ParadoxCloneBlackboardKeys::LastModeTransitionReason, UBlackboardKeyType_Name::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationResponseRuleId, UBlackboardKeyType_Name::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationPriority, UBlackboardKeyType_Int::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationRevision, UBlackboardKeyType_Int::StaticClass() },
		{ ParadoxCloneBlackboardKeys::HasValidInvestigation, UBlackboardKeyType_Bool::StaticClass() },
		{ ParadoxCloneBlackboardKeys::ReplayResumeAvailable, UBlackboardKeyType_Bool::StaticClass() },
		{ ParadoxCloneBlackboardKeys::InvestigationConfidence, UBlackboardKeyType_Float::StaticClass() }
	};
	for (const FRequiredKey& Key : Required)
	{
		const FBlackboard::FKey KeyId = Data->GetKeyID(Key.Name);
		const TSubclassOf<UBlackboardKeyType> KeyType =
			KeyId != FBlackboard::InvalidKey
				? Data->GetKeyType(KeyId)
				: TSubclassOf<UBlackboardKeyType>();
		if (!KeyType || !KeyType->IsChildOf(Key.Type))
		{
			OutDiagnostic = FString::Printf(
				TEXT("Blackboard key '%s' is missing or has the wrong native type."),
				*Key.Name.ToString());
			return false;
		}
		const FBlackboardEntry* Entry = Data->GetKey(KeyId);
		if (Key.ExpectedEnum)
		{
			const UBlackboardKeyType_Enum* EnumKey = Entry
				? Cast<UBlackboardKeyType_Enum>(Entry->KeyType)
				: nullptr;
			if (!EnumKey || EnumKey->EnumType != Key.ExpectedEnum)
			{
				OutDiagnostic = FString::Printf(
					TEXT("Blackboard key '%s' must use enum '%s'."),
					*Key.Name.ToString(),
					*Key.ExpectedEnum->GetName());
				return false;
			}
		}
		if (Key.ExpectedObjectBase)
		{
			const UBlackboardKeyType_Object* ObjectKey = Entry
				? Cast<UBlackboardKeyType_Object>(Entry->KeyType)
				: nullptr;
			if (!ObjectKey
				|| ObjectKey->BaseClass != Key.ExpectedObjectBase)
			{
				OutDiagnostic = FString::Printf(
					TEXT("Blackboard key '%s' must use Object base class '%s'."),
					*Key.Name.ToString(),
					*Key.ExpectedObjectBase->GetName());
				return false;
			}
		}
	}
	return true;
}
