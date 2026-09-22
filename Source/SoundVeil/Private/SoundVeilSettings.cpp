// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilSettings.h"

USoundVeilSettings::USoundVeilSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("SoundVeil");
}

FName USoundVeilSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName USoundVeilSettings::GetSectionName() const
{
	return TEXT("SoundVeil");
}

const USoundVeilSettings& USoundVeilSettings::Get()
{
	const USoundVeilSettings* Settings = GetDefault<USoundVeilSettings>();
	check(Settings);
	return *Settings;
}
