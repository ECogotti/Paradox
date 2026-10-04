#include "Environment/ParadoxBlackHoleMaterialVariants.h"

#include "Materials/MaterialInterface.h"

uint8 UParadoxBlackHoleMaterialVariants::NormalizeMask(uint8 InMask)
{
	if (!(InMask & 2))
	{
		InMask &= ~4;
	}
	if (!(InMask & 1))
	{
		InMask &= ~(8 | 16 | 32 | 64);
	}
	return InMask;
}

UMaterialInterface* UParadoxBlackHoleMaterialVariants::FindVariant(const uint8 InMask) const
{
	const uint8 NormalizedMask = NormalizeMask(InMask);
	UMaterialInterface* Result = nullptr;
	for (const FParadoxBlackHoleMaterialVariant& Entry : Variants)
	{
		if (Entry.FeatureMask == NormalizedMask)
		{
			if (Result || !IsValid(Entry.Material))
			{
				return nullptr;
			}
			Result = Entry.Material;
		}
	}
	return Result;
}

bool UParadoxBlackHoleMaterialVariants::FindSourceDefaults(UMaterialInterface* Source, uint8& OutMask) const
{
	if (!IsValid(Source))
	{
		return false;
	}
	for (const FParadoxBlackHoleMaterialVariant& Entry : Variants)
	{
		if (Entry.Material == Source)
		{
			OutMask = Entry.FeatureMask;
			return true;
		}
	}
	for (const FParadoxBlackHoleMaterialVariant& Entry : SourceProfiles)
	{
		if (IsValid(Entry.Material) && Entry.Material->GetMaterial() == Source->GetMaterial())
		{
			OutMask = Entry.FeatureMask;
			return true;
		}
	}
	return false;
}
