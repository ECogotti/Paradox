#include "Types/FootstepTypes.h"

FFootstepRequest FFootstepRequest::GetSanitized() const
{
	FFootstepRequest Sanitized = *this;
	Sanitized.NormalizedIntensity = FMath::IsFinite(NormalizedIntensity)
		? FMath::Clamp(NormalizedIntensity, 0.0f, 1.0f)
		: 1.0f;
	return Sanitized;
}
