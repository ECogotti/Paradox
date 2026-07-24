#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * Keeps the authored Property Bag tied to its Definition while leaving runtime schemas immutable.
 *
 * A Definition change performs a value-preserving migration. Bags loaded from older assets are
 * visibly flagged until the designer changes/reselects the Definition, avoiding silent request loss.
 */
class FGameplayActionExecutionSpecCustomization final : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> StructPropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	void HandleDefinitionChanged();
	EVisibility GetStaleWarningVisibility() const;

	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> ParametersHandle;
};
