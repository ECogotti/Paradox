#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogParadoxMaterialExpressions, Log, All);

#define PARADOXMATERIALEXPRESSIONS_LOG_INFO(Format, ...) \
    UE_LOG(LogParadoxMaterialExpressions, Log, TEXT(Format), ##__VA_ARGS__)

#define PARADOXMATERIALEXPRESSIONS_LOG_WARNING(Format, ...) \
    UE_LOG(LogParadoxMaterialExpressions, Warning, TEXT(Format), ##__VA_ARGS__)

#define PARADOXMATERIALEXPRESSIONS_LOG_ERROR(Format, ...) \
    UE_LOG(LogParadoxMaterialExpressions, Error, TEXT(Format), ##__VA_ARGS__)

class FParadoxMaterialExpressionsModule final : public IModuleInterface
{
};
