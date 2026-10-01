#pragma once

#include "Blueprint/UserWidget.h"
#include "ParadoxHackingLetterWidget.generated.h"

class UCommonButtonBase;
class UCommonTextBlock;
DECLARE_MULTICAST_DELEGATE_TwoParams(FParadoxHackingLetterClicked, int32, const FString&);

/** Logic only. A Blueprint provides the Common UI button and text, including all presentation. */
UCLASS(Abstract, Blueprintable)
class PARADOX_API UParadoxHackingLetterWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	bool HasRequiredBindings() const;
	void UpdateLetter(int32 InSlotIndex, const FString& InLetter, bool bInFrozen, bool bAvailable);
	FParadoxHackingLetterClicked OnLetterClicked;
protected:
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UCommonButtonBase> LetterButton;
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UCommonTextBlock> LetterText;
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnFrozenStateChanged(bool bIsFrozen);
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
private:
	UFUNCTION() void HandleClicked();
	int32 SlotIndex = INDEX_NONE;
	FString CurrentLetter;
	bool bFrozen = false;
};
