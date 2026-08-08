#include "Interaction/ParadoxInteractionActionDefinition.h"

#include "GameplayActionTags.h"
#include "Interaction/ParadoxEmitterInteractionAction.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "Interaction/ParadoxReceiverInteractionAction.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Paradox.h"
#include "StructUtils/PropertyBag.h"
#include "StructUtils/StructView.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace ParadoxInteractionActionParameters
{
	const FName Target(TEXT("Target"));
	const FName InteractionTag(TEXT("InteractionTag"));
	const FName NavigationFilter(TEXT("NavigationFilter"));
	const FName AcceptanceRadius(TEXT("AcceptanceRadius"));
	const FName AllowStrafe(TEXT("bAllowStrafe"));
	const FName ReceiverComponentName(TEXT("ReceiverComponentName"));
	const FName EmitterComponentName(TEXT("EmitterComponentName"));
	const FName SignalTag(TEXT("SignalTag"));
	const FName Command(TEXT("Command"));
}

namespace UE::Paradox::InteractionDefinition::Private
{
	TArray<FPropertyBagPropertyDesc> MakeCommonDescriptors()
	{
		using namespace ParadoxInteractionActionParameters;
		return {
			{Target, EPropertyBagPropertyType::SoftObject, AActor::StaticClass()},
			{InteractionTag, EPropertyBagPropertyType::Struct, FGameplayTag::StaticStruct()},
			{NavigationFilter, EPropertyBagPropertyType::Class, UNavigationQueryFilter::StaticClass()},
			{AcceptanceRadius, EPropertyBagPropertyType::Float},
			{AllowStrafe, EPropertyBagPropertyType::Bool}
		};
	}

	void InitializeMovementDefaults(FInstancedPropertyBag& Bag)
	{
		using namespace ParadoxInteractionActionParameters;
		Bag.SetValueClass(NavigationFilter, nullptr);
		Bag.SetValueFloat(AcceptanceRadius, -1.0f);
		Bag.SetValueBool(AllowStrafe, false);
	}
}

UParadoxInteractionActionDefinition::UParadoxInteractionActionDefinition()
{
	ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Interaction);
	BlockedPolicy = EGameplayActionBlockedPolicy::Reject;
	bInterruptible = true;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;

	const TArray<FPropertyBagPropertyDesc> Descriptors =
		UE::Paradox::InteractionDefinition::Private::MakeCommonDescriptors();
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
	UE::Paradox::InteractionDefinition::Private::InitializeMovementDefaults(DefaultParameters);
}

void UParadoxInteractionActionDefinition::PostLoad()
{
	Super::PostLoad();
	const UGameplayActionDefinition* NativeDefaults =
		GetClass()->GetDefaultObject<UGameplayActionDefinition>();
	if (this != NativeDefaults && NativeDefaults
		&& !DefaultParameters.HasSameLayout(NativeDefaults->DefaultParameters))
	{
		DefaultParameters.MigrateToNewBagInstance(NativeDefaults->DefaultParameters);
	}
}

#if WITH_EDITOR
EDataValidationResult UParadoxInteractionActionDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	const auto Require = [this, &Context, &Result](
		const FName Name,
		const EPropertyBagPropertyType Type,
		const UObject* TypeObject)
	{
		const FPropertyBagPropertyDesc* Desc = DefaultParameters.FindPropertyDescByName(Name);
		if (!Desc || Desc->ValueType != Type || Desc->ValueTypeObject != TypeObject)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Standard interaction Definition parameter '%s' is missing or has the wrong type."),
				*Name.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	};
	using namespace ParadoxInteractionActionParameters;
	Require(Target, EPropertyBagPropertyType::SoftObject, AActor::StaticClass());
	Require(InteractionTag, EPropertyBagPropertyType::Struct, FGameplayTag::StaticStruct());
	Require(NavigationFilter, EPropertyBagPropertyType::Class, UNavigationQueryFilter::StaticClass());
	Require(AcceptanceRadius, EPropertyBagPropertyType::Float, nullptr);
	Require(AllowStrafe, EPropertyBagPropertyType::Bool, nullptr);
	if (!ExecutionLocks.HasTagExact(GameplayActionTags::Lock_Movement)
		|| !ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Interaction))
	{
		Context.AddError(FText::FromString(TEXT("Standard interaction Definitions require both Movement and Interaction execution locks.")));
		Result = EDataValidationResult::Invalid;
	}
	if (BlockedPolicy != EGameplayActionBlockedPolicy::Reject)
	{
		Context.AddError(FText::FromString(TEXT("Standard interaction Definitions require Reject blocked policy to prevent repeated clicks from queuing.")));
		Result = EDataValidationResult::Invalid;
	}

	if (IsA<UParadoxReceiverInteractionActionDefinition>())
	{
		Require(ReceiverComponentName, EPropertyBagPropertyType::Name, nullptr);
		Require(Command, EPropertyBagPropertyType::Enum, StaticEnum<EParadoxInteractionStateCommand>());
	}
	if (IsA<UParadoxEmitterInteractionActionDefinition>())
	{
		Require(EmitterComponentName, EPropertyBagPropertyType::Name, nullptr);
		Require(SignalTag, EPropertyBagPropertyType::Struct, FGameplayTag::StaticStruct());
		Require(Command, EPropertyBagPropertyType::Enum, StaticEnum<EParadoxInteractionStateCommand>());
		const TValueOrError<FStructView, EPropertyBagResult> SignalValue =
			DefaultParameters.GetValueStruct(SignalTag, FGameplayTag::StaticStruct());
		const FGameplayTag* AuthoredSignal = SignalValue.HasValue()
			? reinterpret_cast<const FGameplayTag*>(SignalValue.GetValue().GetMemory())
			: nullptr;
		if (!AuthoredSignal || !AuthoredSignal->IsValid())
		{
			Context.AddError(FText::FromString(TEXT("Emitter interaction Definitions require a valid exact SignalTag.")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

UParadoxReceiverInteractionActionDefinition::UParadoxReceiverInteractionActionDefinition()
{
	using namespace ParadoxInteractionActionParameters;
	InstanceClass = UParadoxReceiverInteractionAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_Interaction_Receiver;
	DebugDescription = TEXT("Moves to a Smart Object interaction cell and changes a manual Puzzle Receiver.");
	TArray<FPropertyBagPropertyDesc> Descriptors =
		UE::Paradox::InteractionDefinition::Private::MakeCommonDescriptors();
	Descriptors.Add({ReceiverComponentName, EPropertyBagPropertyType::Name});
	Descriptors.Add({Command, EPropertyBagPropertyType::Enum, StaticEnum<EParadoxInteractionStateCommand>()});
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
	UE::Paradox::InteractionDefinition::Private::InitializeMovementDefaults(DefaultParameters);
	DefaultParameters.SetValueName(ReceiverComponentName, NAME_None);
	DefaultParameters.SetValueEnum(Command, EParadoxInteractionStateCommand::Activate);
}

UParadoxEmitterInteractionActionDefinition::UParadoxEmitterInteractionActionDefinition()
{
	using namespace ParadoxInteractionActionParameters;
	InstanceClass = UParadoxEmitterInteractionAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_Interaction_Emitter;
	DebugDescription = TEXT("Moves to a Smart Object interaction cell and publishes a Puzzle Emitter signal.");
	TArray<FPropertyBagPropertyDesc> Descriptors =
		UE::Paradox::InteractionDefinition::Private::MakeCommonDescriptors();
	Descriptors.Add({EmitterComponentName, EPropertyBagPropertyType::Name});
	Descriptors.Add({SignalTag, EPropertyBagPropertyType::Struct, FGameplayTag::StaticStruct()});
	Descriptors.Add({Command, EPropertyBagPropertyType::Enum, StaticEnum<EParadoxInteractionStateCommand>()});
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
	UE::Paradox::InteractionDefinition::Private::InitializeMovementDefaults(DefaultParameters);
	DefaultParameters.SetValueName(EmitterComponentName, NAME_None);
	DefaultParameters.SetValueEnum(Command, EParadoxInteractionStateCommand::Activate);
}
