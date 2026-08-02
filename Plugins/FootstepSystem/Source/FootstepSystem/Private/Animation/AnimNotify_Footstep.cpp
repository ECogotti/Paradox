#include "Animation/AnimNotify_Footstep.h"

#include "Components/FootstepComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "FootstepSystemModule.h"
#include "GameFramework/Actor.h"

UAnimNotify_Footstep::UAnimNotify_Footstep()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(60, 170, 255);
	bShouldFireInEditor = false;
#endif
}

void UAnimNotify_Footstep::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	UFootstepComponent* FootstepComponent =
		Owner ? Owner->FindComponentByClass<UFootstepComponent>() : nullptr;
	if (!FootstepComponent)
	{
#if !UE_BUILD_SHIPPING
		if (IsFootstepSystemDebugEnabled())
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Footstep notify on mesh '%s' could not find UFootstepComponent on owner '%s'."),
				*GetNameSafe(MeshComp),
				*GetNameSafe(Owner));
		}
#endif
		return;
	}

	FFootstepRequest Request;
	Request.Foot = Foot;
	Request.SocketOverride = SocketOverride;
	Request.NormalizedIntensity = NormalizedIntensity;
	FootstepComponent->SubmitFootstepRequestFromAnimation(Request, MeshComp);
}

FString UAnimNotify_Footstep::GetNotifyName_Implementation() const
{
	switch (Foot)
	{
	case EFootstepFoot::Left:
		return TEXT("Footstep (Left)");
	case EFootstepFoot::Right:
		return TEXT("Footstep (Right)");
	default:
		return TEXT("Footstep");
	}
}
