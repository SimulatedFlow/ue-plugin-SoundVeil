// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "SoundVeilProfile.generated.h"

/**
 * How a material sounds. One asset per kind of wall, not one per sound.
 *
 * Concrete, a curtain and a pane of glass all block the same rays - what differs is what happens to
 * the sound once they do. That difference is two curves, and it belongs in an asset a sound designer
 * can drag a handle on, not in a constant somebody has to recompile. A curtain barely touches the
 * volume and shaves the top off; concrete takes most of the volume and nearly all of the top.
 *
 * The trace side of the asset (channel, taps, pattern) is the other half: a source that only cares
 * about walls traces on a channel that only walls block, and the pattern width says how big an
 * opening has to be before it counts as one.
 */
UCLASS(BlueprintType, meta = (DisplayName = "SoundVeil Profile"))
class SOUNDVEIL_API USoundVeilProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	USoundVeilProfile();

	// --------------------------------------------------------------------------------------------------
	// Tracing
	// --------------------------------------------------------------------------------------------------

	/**
	 * Channel the occlusion taps trace on.
	 *
	 * Visibility is the sane default: it is blocked by walls and ignored by most of the small
	 * decoration a level is full of. Give the plugin its own channel if you want a piece of geometry
	 * to be visually solid but acoustically open (a grate, a railing) or the other way round.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/**
	 * Rays per measurement.
	 *
	 * One ray is what the engine's own occlusion does, and one ray can only ever answer yes or no: a
	 * half open door is a wall until the ray slips through the gap, and then it is open air. Five rays
	 * across a small pattern give four fifths, two fifths, one fifth - the values a door actually
	 * passes through while it swings. Odd numbers are the better choice; the first tap is always the
	 * straight centre line, so an odd count keeps the pattern symmetric around it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing", meta = (ClampMin = "1", UIMax = "16"))
	int32 TapCount = 5;

	/**
	 * Radius of the tap pattern in cm, measured at the source.
	 *
	 * This is the size of the smallest opening the measurement can resolve. Too small and five taps
	 * all go through the same gap and you are back to a yes/no answer; too large and the taps reach
	 * round corners into rooms the sound is not in. Roughly the width of a doorway works: 60 cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing", meta = (ClampMin = "0.0", UIMax = "500.0"))
	float PatternRadius = 60.0f;

	/**
	 * Nothing closer than this to the listener is ever occluded, in cm.
	 *
	 * A source the player is standing on top of has geometry between the two points only because the
	 * two points are almost the same point. Below this distance the measurement is skipped and the
	 * source runs open.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing", meta = (ClampMin = "0.0", UIMax = "2000.0"))
	float MinDistance = 120.0f;

	/**
	 * Past this distance a source stops asking for trace budget, in cm.
	 *
	 * It keeps whatever value it had, which is the right answer for something you cannot hear anyway,
	 * and the budget goes to the sources near the player instead. Set it a little beyond the sound's
	 * own attenuation radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing", meta = (ClampMin = "0.0", UIMax = "100000.0"))
	float MaxAudibleDistance = 8000.0f;

	/**
	 * How hard this source fights for the shared budget, 0..1.
	 *
	 * Purely a scheduling weight: a priority of 1 next to a priority of 0.25 means the first source is
	 * measured more often, never that it is louder. Loudness and distance are already in the score, so
	 * this is for the cases they get wrong - the quiet distant radio the plot depends on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tracing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Priority = 1.0f;

	// --------------------------------------------------------------------------------------------------
	// Smoothing
	// --------------------------------------------------------------------------------------------------

	/**
	 * Seconds the occlusion takes to rise towards a newly blocked measurement.
	 *
	 * Short, because a door closing should sound like a door closing. Zero is legal and snaps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smoothing", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float AttackSeconds = 0.15f;

	/**
	 * Seconds the occlusion takes to fall back towards a clear measurement.
	 *
	 * Longer than the attack on purpose. Somebody walking through the line of sight blocks it for two
	 * frames; with a slow release those two frames never make it to the mix as a click.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smoothing", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float ReleaseSeconds = 0.35f;

	// --------------------------------------------------------------------------------------------------
	// Response
	// --------------------------------------------------------------------------------------------------

	/**
	 * Occlusion 0..1 in, volume multiplier 0..1 out.
	 *
	 * The default drops a fully covered source to a quarter of its volume rather than to silence,
	 * because a wall you can still faintly hear through is what a wall sounds like. Pull the end of
	 * the curve to zero for a vault door.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (XAxisName = "Occlusion", YAxisName = "Volume"))
	FRuntimeFloatCurve OcclusionToVolume;

	/**
	 * Occlusion 0..1 in, low pass cut-off in Hz out.
	 *
	 * This is the half of the effect people actually hear as "behind a wall". Volume alone makes a
	 * source sound far away; losing the top end makes it sound covered. 20 kHz means the filter is
	 * effectively off, which is what an unoccluded source gets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Response", meta = (XAxisName = "Occlusion", YAxisName = "Hz"))
	FRuntimeFloatCurve OcclusionToLowpass;

	// --------------------------------------------------------------------------------------------------
	// Queries
	// --------------------------------------------------------------------------------------------------

	/** Volume multiplier for an occlusion value, curve and clamping applied. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Profile")
	float EvaluateVolume(float Occlusion) const;

	/** Low pass cut-off in Hz for an occlusion value, curve and clamping applied. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Profile")
	float EvaluateLowpass(float Occlusion) const;

	/** Fill both curves with the built-in defaults. Called by the constructor, and by the reset button. */
	UFUNCTION(CallInEditor, Category = "Response")
	void ResetCurvesToDefaults();

	/** The lowest cut-off the filter is ever driven to, in Hz. Below this a source turns to mud. */
	static constexpr float MinLowpassFrequency = 20.0f;

	/** Cut-off that means "filter off". Matches what UAudioComponent treats as unfiltered. */
	static constexpr float MaxLowpassFrequency = 20000.0f;
};
