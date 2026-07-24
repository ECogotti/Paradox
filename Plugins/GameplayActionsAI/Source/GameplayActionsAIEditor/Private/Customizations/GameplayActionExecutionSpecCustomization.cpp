#include "Customizations/GameplayActionExecutionSpecCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Types/GameplayActionExecutionSpec.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GameplayActionExecutionSpecCustomization"

TSharedRef<IPropertyTypeCustomization> FGameplayActionExecutionSpecCustomization::MakeInstance()
{
	return MakeShared<FGameplayActionExecutionSpecCustomization>();
}

void FGameplayActionExecutionSpecCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			StructPropertyHandle->CreatePropertyValueWidget()
		];
}

void FGameplayActionExecutionSpecCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	const TSharedPtr<IPropertyHandle> DefinitionHandle =
		StructPropertyHandle->GetChildHandle(
			GET_MEMBER_NAME_CHECKED(FGameplayActionExecutionSpec, Definition));
	ParametersHandle = StructPropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FGameplayActionExecutionSpec, Parameters));

	if (DefinitionHandle)
	{
		DefinitionHandle->SetOnPropertyValueChanged(
			FSimpleDelegate::CreateSP(
				this,
				&FGameplayActionExecutionSpecCustomization::HandleDefinitionChanged));
	}

	ChildBuilder.AddCustomRow(LOCTEXT("StaleSchemaSearch", "Stale Parameter Schema"))
		.Visibility(TAttribute<EVisibility>::CreateSP(
			this,
			&FGameplayActionExecutionSpecCustomization::GetStaleWarningVisibility))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"StaleSchemaWarning",
				"Parameter schema is stale. Reselect the Definition to migrate compatible values."))
			.ColorAndOpacity(FLinearColor(1.0f, 0.25f, 0.1f))
			.AutoWrapText(true)
		];

	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		if (TSharedPtr<IPropertyHandle> Child = StructPropertyHandle->GetChildHandle(Index))
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}
}

void FGameplayActionExecutionSpecCustomization::HandleDefinitionChanged()
{
	if (!StructHandle)
	{
		return;
	}

	TArray<void*> RawData;
	StructHandle->AccessRawData(RawData);
	if (ParametersHandle)
	{
		ParametersHandle->NotifyPreChange();
	}
	for (void* RawValue : RawData)
	{
		if (FGameplayActionExecutionSpec* Spec =
			static_cast<FGameplayActionExecutionSpec*>(RawValue))
		{
			Spec->SynchronizeParameters();
		}
	}
	if (ParametersHandle)
	{
		ParametersHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		ParametersHandle->NotifyFinishedChangingProperties();
	}
}

EVisibility FGameplayActionExecutionSpecCustomization::GetStaleWarningVisibility() const
{
	if (!StructHandle)
	{
		return EVisibility::Collapsed;
	}

	TArray<void*> RawData;
	StructHandle->AccessRawData(RawData);
	for (const void* RawValue : RawData)
	{
		const FGameplayActionExecutionSpec* Spec =
			static_cast<const FGameplayActionExecutionSpec*>(RawValue);
		if (Spec && Spec->Definition && !Spec->IsSchemaSynchronized())
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
