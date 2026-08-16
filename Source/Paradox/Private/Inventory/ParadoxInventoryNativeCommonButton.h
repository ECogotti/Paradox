#pragma once

#include "CommonButtonBase.h"
#include "CoreMinimal.h"
#include "ParadoxInventoryNativeCommonButton.generated.h"

class UCommonTextBlock;
class UImage;

/** Concrete Common Button used only by the native inventory-widget fallbacks. */
UCLASS(Transient, NotBlueprintable)
class UParadoxInventoryNativeCommonButton final : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	void BuildActionContent(UImage*& OutIcon, UCommonTextBlock*& OutLabel);
	void BuildTextContent(const FText& Label);

private:
	void EnsureNativeWidgetTree();
};
