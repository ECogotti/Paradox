#include "Data/EntityRelationPolicySet.h"

#include "Misc/DataValidation.h"
#include "Policies/EntityRelationPolicy.h"

#define LOCTEXT_NAMESPACE "EntityRelationPolicySet"

namespace
{
	void AddIssue(FEntityRelationValidationResult& Result, EEntityRelationIssueSeverity Severity, FName Code, FString Message)
	{
		FEntityRelationIssue& Issue = Result.Issues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.Message = MoveTemp(Message);
	}
}

FEntityRelationValidationResult UEntityRelationPolicySet::ValidatePolicySet() const
{
	FEntityRelationValidationResult Result;
	if (Policies.IsEmpty())
	{
		AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("NoPolicies"), TEXT("The Policy Set contains no policies."));
		return Result;
	}

	TSet<FName> SeenIds;
	TMap<int32, int32> PriorityCounts;
	int32 EnabledCount = 0;
	for (int32 Index = 0; Index < Policies.Num(); ++Index)
	{
		const UEntityRelationPolicy* Policy = Policies[Index];
		if (!Policy)
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("NullPolicy"), FString::Printf(TEXT("Policy at index %d is null."), Index));
			continue;
		}
		if (Policy->GetPolicyId().IsNone())
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("MissingPolicyId"), FString::Printf(TEXT("Policy at index %d has no PolicyId."), Index));
		}
		else if (SeenIds.Contains(Policy->GetPolicyId()))
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("DuplicatePolicyId"), FString::Printf(TEXT("PolicyId '%s' is duplicated."), *Policy->GetPolicyId().ToString()));
		}
		else
		{
			SeenIds.Add(Policy->GetPolicyId());
		}
		if (Policy->GetSupportedDomains().IsEmpty())
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("MissingDomains"), FString::Printf(TEXT("Policy '%s' supports no domains."), *Policy->GetPolicyId().ToString()));
		}
		if (Policy->IsPolicyEnabled())
		{
			++EnabledCount;
			++PriorityCounts.FindOrAdd(Policy->GetPriority());
		}
		else
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Info, TEXT("DisabledPolicy"), FString::Printf(TEXT("Policy '%s' is disabled."), *Policy->GetPolicyId().ToString()));
		}
	}

	if (EnabledCount == 0)
	{
		AddIssue(Result, EEntityRelationIssueSeverity::Error, TEXT("NoEnabledPolicies"), TEXT("The Policy Set contains no enabled policies."));
	}
	for (const TPair<int32, int32>& Pair : PriorityCounts)
	{
		if (Pair.Value > 1)
		{
			AddIssue(Result, EEntityRelationIssueSeverity::Warning, TEXT("DuplicatePriority"), FString::Printf(TEXT("%d enabled policies share priority %d; serialized order will break the tie."), Pair.Value, Pair.Key));
		}
	}
	return Result;
}

#if WITH_EDITOR
EDataValidationResult UEntityRelationPolicySet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Validation = Super::IsDataValid(Context);
	if (Validation == EDataValidationResult::NotValidated)
	{
		Validation = EDataValidationResult::Valid;
	}
	const FEntityRelationValidationResult Result = ValidatePolicySet();
	for (const FEntityRelationIssue& Issue : Result.Issues)
	{
		if (Issue.Severity == EEntityRelationIssueSeverity::Error)
		{
			Context.AddError(FText::FromString(Issue.Message));
			Validation = EDataValidationResult::Invalid;
		}
		else if (Issue.Severity == EEntityRelationIssueSeverity::Warning)
		{
			Context.AddWarning(FText::FromString(Issue.Message));
		}
	}
	return Validation;
}
#endif

#undef LOCTEXT_NAMESPACE
