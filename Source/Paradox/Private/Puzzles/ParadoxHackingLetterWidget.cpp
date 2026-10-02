#include "Puzzles/ParadoxHackingLetterWidget.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Paradox.h"

bool UParadoxHackingLetterWidget::HasRequiredBindings() const { return LetterButton && LetterText; }

void UParadoxHackingLetterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (LetterButton)
	{
		LetterButton->OnClicked().RemoveAll(this);
		LetterButton->OnClicked().AddUObject(this, &ThisClass::HandleClicked);
	}
	if (!HasRequiredBindings())
	{
		PARADOX_LOG_ERROR(TEXT("Hacking letter widget '%s' requires BindWidget LetterButton (CommonButtonBase) and LetterText (CommonTextBlock)."), *GetName());
		if (LetterButton) { LetterButton->SetIsInteractionEnabled(false); }
	}
}

void UParadoxHackingLetterWidget::NativeDestruct()
{
	if (LetterButton) { LetterButton->OnClicked().RemoveAll(this); }
	OnLetterClicked.Clear();
	Super::NativeDestruct();
}

void UParadoxHackingLetterWidget::UpdateLetter(int32 InSlotIndex, const FString& InLetter, bool bInFrozen, bool bAvailable)
{
	SlotIndex = InSlotIndex; CurrentLetter = InLetter;
	if (LetterText) { LetterText->SetText(FText::FromString(CurrentLetter)); }
	if (LetterButton) { LetterButton->SetIsInteractionEnabled(HasRequiredBindings() && bAvailable && !bInFrozen); }
	if (bFrozen != bInFrozen) { bFrozen = bInFrozen; OnFrozenStateChanged(bFrozen); }
}

void UParadoxHackingLetterWidget::HandleClicked()
{
	if (HasRequiredBindings() && LetterButton->GetIsEnabled() && LetterButton->IsInteractionEnabled() && !bFrozen) { OnLetterClicked.Broadcast(SlotIndex, CurrentLetter); }
}
