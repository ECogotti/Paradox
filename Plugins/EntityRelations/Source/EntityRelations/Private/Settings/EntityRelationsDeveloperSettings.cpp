#include "Settings/EntityRelationsDeveloperSettings.h"

UEntityRelationsDeveloperSettings::UEntityRelationsDeveloperSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Entity Relations");
}

FName UEntityRelationsDeveloperSettings::GetCategoryName() const
{
	return TEXT("Game");
}
