#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "Types/FootstepTypes.h"
#include "AnimNotify_Footstep.generated.h"

/**
 * Animation-synchronized foot-contact marker.
 *
 * The notify only identifies the request and delegates all world interaction to UFootstepComponent.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Footstep"))
class FOOTSTEPSYSTEM_API UAnimNotify_Footstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_Footstep();

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System")
	EFootstepFoot Foot = EFootstepFoot::Unspecified;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System")
	FName SocketOverride = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalizedIntensity = 1.0f;
};
