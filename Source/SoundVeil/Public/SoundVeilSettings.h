// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SoundVeilSettings.generated.h"

class USoundVeilProfile;

/**
 * Project-wide defaults for the occlusion service.
 * Edit under Project Settings > Plugins > SoundVeil; stored in DefaultGame.ini.
 *
 * Read once, when a world's subsystem comes up. Budget, tap count, bypass and the counter box can all
 * be moved afterwards at runtime - through the subsystem, the Blueprint library or the SoundVeil.*
 * console commands - without touching the config.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SoundVeil"))
class SOUNDVEIL_API USoundVeilSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USoundVeilSettings();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** Convenience accessor. Never returns null. */
	static const USoundVeilSettings& Get();

	// --------------------------------------------------------------------------------------------------
	// Budget
	// --------------------------------------------------------------------------------------------------

	/**
	 * Hard cap on traces started in one frame, shared by every source in the world.
	 *
	 * Sixty ambient sources with one ray each is sixty traces a frame, forty of them for sources the
	 * player cannot hear. This is the number that replaces the sixty. Whoever does not get a turn keeps
	 * the value it already had, so nothing stalls and nothing pops - it is measured less often.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "256"))
	int32 TraceBudgetPerFrame = 8;

	/**
	 * Taps per measurement for sources whose profile does not say otherwise, and the value the
	 * SoundVeil.Taps console command overrides.
	 *
	 * A measurement costs this many traces. If fewer are left in the frame's ration the measurement
	 * still happens with what is left - a source is never skipped for want of a full pattern, it is
	 * measured more coarsely. At one tap the answer is binary, which is exactly what the engine's own
	 * occlusion gives you, and makes for an honest side-by-side.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", UIMax = "16"))
	int32 DefaultTapCount = 5;

	/**
	 * Most turns one source can hold in a scheduling round, 1..8.
	 *
	 * A round contains every source at least once - that is what bounds the wait - and the loud, near,
	 * high priority ones up to this many times. At 1 the schedule is plain round robin and everybody is
	 * measured at the same rate.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxTurnsPerSource = 3;

	/**
	 * How many sources are tracked with their own occlusion value.
	 *
	 * Everything past this is not dropped: it is grouped, shares one value, and shows up as a single
	 * collected line in the counter box. A level with four hundred ambient loops still sounds right;
	 * the four hundredth simply does not get its own answer.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", UIMax = "1024"))
	int32 MaxSources = 128;

	/**
	 * Fire the taps as async traces and read them back on the following frame.
	 *
	 * On by default, and it is the whole reason the plugin is affordable: a synchronous trace inside the
	 * audio update blocks the game thread on the physics scene, which is precisely the cost this plugin
	 * exists to remove. Turn it off only if you need the answer inside the same frame you asked - the
	 * traces then run synchronously and the budget still holds.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget")
	bool bUseAsyncTraces = true;

	// --------------------------------------------------------------------------------------------------
	// Smoothing
	// --------------------------------------------------------------------------------------------------

	/** Attack for sources with no profile, in seconds. See USoundVeilProfile::AttackSeconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Smoothing", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float DefaultAttackSeconds = 0.15f;

	/** Release for sources with no profile, in seconds. See USoundVeilProfile::ReleaseSeconds. */
	UPROPERTY(Config, EditAnywhere, Category = "Smoothing", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float DefaultReleaseSeconds = 0.35f;

	/** Tap pattern radius for sources with no profile, in cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Smoothing", meta = (ClampMin = "0.0", UIMax = "500.0"))
	float DefaultPatternRadius = 60.0f;

	// --------------------------------------------------------------------------------------------------
	// Defaults
	// --------------------------------------------------------------------------------------------------

	/**
	 * Profile used by a source component that has none of its own. Leave it empty and a built-in
	 * profile is used instead, so the plugin works before any asset exists.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Defaults", meta = (AllowedClasses = "/Script/SoundVeil.SoundVeilProfile"))
	TSoftObjectPtr<USoundVeilProfile> DefaultProfile;

	// --------------------------------------------------------------------------------------------------
	// Display
	// --------------------------------------------------------------------------------------------------

	/** Show the counter box as soon as a world's service comes up. Same as SoundVeil.Show 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Display")
	bool bShowStatsByDefault = false;

	/**
	 * How many individual sources the counter box lists under the summary lines.
	 *
	 * Without numbers on screen an audio plugin cannot be shown in a still image at all - the whole
	 * effect is inaudible in a screenshot. This list is what makes it visible.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "0", ClampMax = "24"))
	int32 StatsSourceLines = 6;

	/**
	 * Draw the counter box in an editor viewport through the debug draw service as well as through
	 * AHUD. The AHUD path is the one that ships; this one is what makes a screenshot inside the editor
	 * possible without setting a HUD class first. Compiled out entirely outside the editor.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Display")
	bool bEnableEditorViewportStats = true;
};
