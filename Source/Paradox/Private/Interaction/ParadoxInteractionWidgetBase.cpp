#include "Interaction/ParadoxInteractionWidgetBase.h"

#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Paradox.h"

APawn* UParadoxInteractionWidgetBase::GetCurrentRequester() const
{
	return OwningPlayerController.IsValid() ? OwningPlayerController->GetPawn() : nullptr;
}

void UParadoxInteractionWidgetBase::RefreshInteractionOptions()
{
	if (UParadoxSelectionComponent* Selection = SelectionComponent.Get())
	{
		Selection->RefreshSelectedInteractionOptions();
		return;
	}

	CachedInteractionOptions = FParadoxInteractionQueryResult();
	CachedInteractionOptions.DiagnosticMessage =
		TEXT("The widget has no active selection context.");
	OnInteractionOptionsRefreshed(CachedInteractionOptions);
}

FParadoxInteractionAvailabilityResult UParadoxInteractionWidgetBase::GetInteractionAvailability(
	const FGameplayTag InteractionTag) const
{
	if (const UParadoxSelectionComponent* Selection = SelectionComponent.Get())
	{
		return Selection->GetSelectedInteractionAvailability(InteractionTag);
	}
	FParadoxInteractionAvailabilityResult Missing;
	Missing.InteractionTag = InteractionTag;
	Missing.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
	Missing.DiagnosticMessage = TEXT("The widget has no active selection context.");
	return Missing;
}

TArray<FParadoxInteractionAvailabilityResult> UParadoxInteractionWidgetBase::GetInteractionAvailabilities() const
{
	if (const UParadoxSelectionComponent* Selection = SelectionComponent.Get())
	{
		return Selection->GetSelectedInteractionAvailabilities();
	}
	return {};
}

void UParadoxInteractionWidgetBase::RefreshInteractionAvailability()
{
	RefreshInteractionOptions();
}

bool UParadoxInteractionWidgetBase::CanRequestInteraction(
	const FGameplayTag InteractionTag) const
{
	return GetInteractionAvailability(InteractionTag).IsAvailable();
}

FParadoxInteractionRequestResult UParadoxInteractionWidgetBase::RequestInteraction(
	const FGameplayTag InteractionTag)
{
	FParadoxInteractionRequestResult Result;
	UParadoxInteractionComponent* Interaction = InteractionComponent.Get();
	APawn* Requester = GetCurrentRequester();
	APlayerController* PlayerController = OwningPlayerController.Get();
	if (IsValid(Interaction) && IsValid(Requester))
	{
		Result = Interaction->RequestInteraction(
			Requester,
			InteractionTag,
			ParadoxGameplayTags::Origin_Player,
			PlayerController);
	}
	else
	{
		Result.Status = EParadoxInteractionRequestStatus::InvalidRequester;
		Result.DiagnosticMessage =
			TEXT("The widget has no valid selected interaction target or requester Pawn.");
	}

	if (Result.IsAccepted())
	{
		OnInteractionRequestAccepted(Result);
	}
	else
	{
		OnInteractionRequestRejected(Result);
	}
	RefreshInteractionOptions();
	return Result;
}

void UParadoxInteractionWidgetBase::NativeDestruct()
{
	ClearSelectionContext();
	Super::NativeDestruct();
}

void UParadoxInteractionWidgetBase::AssignSelectionContext(
	AActor* InSelectedActor,
	UParadoxSelectableComponent* InSelectableComponent,
	UParadoxSelectionComponent* InSelectionComponent,
	APlayerController* InOwningPlayerController)
{
	if (SelectedActor.Get() == InSelectedActor
		&& SelectableComponent.Get() == InSelectableComponent
		&& SelectionComponent.Get() == InSelectionComponent
		&& OwningPlayerController.Get() == InOwningPlayerController)
	{
		return;
	}

	ClearSelectionContext();
	SelectedActor = InSelectedActor;
	SelectableComponent = InSelectableComponent;
	SelectionComponent = InSelectionComponent;
	OwningPlayerController = InOwningPlayerController;
	InteractionComponent = IsValid(InSelectedActor)
		? InSelectedActor->FindComponentByClass<UParadoxInteractionComponent>()
		: nullptr;
	if (IsValid(InSelectionComponent))
	{
		InteractionOptionsRefreshedHandle =
			InSelectionComponent->OnSelectedInteractionOptionsRefreshedNative().AddUObject(
				this,
				&ThisClass::HandleInteractionOptionsRefreshed);
		CachedInteractionOptions =
			InSelectionComponent->GetSelectedInteractionOptions();
	}
	OnSelectionContextAssigned();
	OnInteractionOptionsRefreshed(CachedInteractionOptions);
	OnInteractionAvailabilityRefreshed(GetInteractionAvailabilities());
}

void UParadoxInteractionWidgetBase::ClearSelectionContext()
{
	const bool bHadContext = SelectedActor.IsValid()
		|| SelectableComponent.IsValid()
		|| SelectionComponent.IsValid()
		|| OwningPlayerController.IsValid();
	if (UParadoxSelectionComponent* Selection = SelectionComponent.Get())
	{
		Selection->OnSelectedInteractionOptionsRefreshedNative().Remove(
			InteractionOptionsRefreshedHandle);
	}
	InteractionOptionsRefreshedHandle.Reset();
	CachedInteractionOptions = FParadoxInteractionQueryResult();
	InteractionComponent.Reset();
	SelectedActor.Reset();
	SelectableComponent.Reset();
	SelectionComponent.Reset();
	OwningPlayerController.Reset();
	if (bHadContext)
	{
		OnSelectionContextCleared();
	}
}

void UParadoxInteractionWidgetBase::HandleInteractionOptionsRefreshed(
	const FParadoxInteractionQueryResult& InteractionOptions)
{
	CachedInteractionOptions = InteractionOptions;
	OnInteractionOptionsRefreshed(CachedInteractionOptions);
	OnInteractionAvailabilityRefreshed(GetInteractionAvailabilities());
}
