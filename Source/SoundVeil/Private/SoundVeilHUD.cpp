// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilHUD.h"

#include "Engine/World.h"
#include "SoundVeilSubsystem.h"

void ASoundVeilHUD::BeginPlay()
{
	Super::BeginPlay();

	if (bShowStatsOnBeginPlay)
	{
		if (const UWorld* World = GetWorld())
		{
			if (USoundVeilSubsystem* Service = World->GetSubsystem<USoundVeilSubsystem>())
			{
				Service->SetShowStats(true);
			}
		}
	}
}

void ASoundVeilHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!bDrawSoundVeilStats)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (USoundVeilSubsystem* Service = World->GetSubsystem<USoundVeilSubsystem>())
		{
			Service->DrawStats(Canvas);
		}
	}
}
