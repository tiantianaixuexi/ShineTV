#include "ShineMCPSettings.h"

UShineMCPSettings::UShineMCPSettings()
{
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("Shine MCP");
}

FName UShineMCPSettings::GetCategoryName() const
{
    return TEXT("Plugins");
}
