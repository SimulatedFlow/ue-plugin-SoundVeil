// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SoundVeilHUD.generated.h"

/**
 * A HUD that draws the counter box, and nothing else.
 *
 * UCanvas rather than UMG on purpose: this box has to survive a cooked Shipping build, because the one
 * thing an audio plugin cannot demonstrate in a screenshot is its audio. The numbers are the proof -
 * traces against budget, the longest a source has waited, and per source the occlusion, the volume
 * factor and the cut-off in Hz.
 *
 * Set it as the HUD class on your game mode, or copy the one line out of DrawHUD into your own HUD:
 *
 *     if (USoundVeilSubsystem* Service = GetWorld()->GetSubsystem<USoundVeilSubsystem>())
 *     {
 *         Service->DrawStats(Canvas);
 *     }
 */
UCLASS(meta = (DisplayName = "SoundVeil HUD"))
class SOUNDVEIL_API ASoundVeilHUD : public AHUD
{
	GENERATED_BODY()

public:
	// AHUD interface
	virtual void DrawHUD() override;

	/** Draw the counter box. The box has its own visibility switch too - SoundVeil.Show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	bool bDrawSoundVeilStats = true;

	/**
	 * Turn the counter box on as soon as this HUD comes up, without needing SoundVeil.Show 1 first.
	 *
	 * On by default here and off in the project settings: somebody who sets this HUD class has asked
	 * for the numbers, and somebody who has not should not get them in their shipping game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	bool bShowStatsOnBeginPlay = true;

protected:
	virtual void BeginPlay() override;
};
