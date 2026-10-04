// Editor validation helpers for fixtures that temporarily modify the modular graph.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Environment/ParadoxBlackHoleMaterialVariants.h"
#include "HAL/IConsoleManager.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstance.h"
#include "Paradox.h"

namespace
{
	FAutoConsoleCommand CacheVariantShaders(
		TEXT("Paradox.BlackHole.CacheVariantShaders"),
		TEXT("Complete editor rendering shader maps after an in-memory graph change; does not save assets."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			// Material statistics can leave a partial shader map after changing a parent.
			// Request all rendering shaders before the harness waits for asset compilation.
			UMaterialInstance::AllMaterialsCacheResourceShadersForRendering(false, true);
		}));

	FAutoConsoleCommand InspectShaders(
		TEXT("Paradox.BlackHole.InspectShaders"), TEXT("Check every catalog entry's SM6 rendering shader map."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const UParadoxBlackHoleMaterialVariants* Catalog = LoadObject<UParadoxBlackHoleMaterialVariants>(nullptr,
				TEXT("/Game/Vfx/BlackHole/DA_BlackHoleMaterialVariants"));
			if (!IsValid(Catalog))
			{
				PARADOX_LOG_ERROR(TEXT("Black hole shader validation failed: compiled variant catalog is unavailable."));
				return;
			}
			int32 Complete = 0;
			for (const FParadoxBlackHoleMaterialVariant& Entry : Catalog->Variants)
			{
				UMaterialInstance* Instance = Cast<UMaterialInstance>(Entry.Material);
				FMaterialResource* Resource = Instance ? Instance->GetMaterialResource(SP_PCD3D_SM6) : nullptr;
				if (Resource && Resource->IsCompilationFinished() && Resource->IsGameThreadShaderMapComplete())
				{
					++Complete;
				}
				else
				{
					PARADOX_LOG_WARNING(TEXT("Black hole variant %02X '%s': SM6 rendering shader map is incomplete."),
					Entry.FeatureMask, *GetPathNameSafe(Instance));
				}
			}
			PARADOX_LOG_INFO(TEXT("BH_SHADER_VALIDATION complete=%d total=%d"), Complete, Catalog->Variants.Num());
		}));
}
#endif
