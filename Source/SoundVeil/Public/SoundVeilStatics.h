// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoundVeilTypes.h"
#include "SoundVeilStatics.generated.h"

class USoundVeilProfile;
class USoundVeilSubsystem;
class UObject;

/**
 * The arithmetic of the plugin, and the Blueprint way in.
 *
 * Everything above the line is static and world-free on purpose: the ranking, the tap pattern, the
 * curves and the smoothing are the four places this plugin can be wrong in a way nobody notices until
 * a level sounds odd. Kept as free functions over plain structs they can be held against a number in a
 * test instead of against a level in a play session, and the subsystem calls exactly these - there is
 * no second copy of the maths hiding inside the tick.
 *
 * Everything below the line is the demo-button half: one call per button, so a Blueprint never has to
 * reach into the subsystem by hand.
 */
UCLASS()
class SOUNDVEIL_API USoundVeilStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ==================================================================================================
	// Pure maths - no world, no assets, no side effects
	// ==================================================================================================

	/**
	 * Occlusion from a tap result: blocked taps over taps fired.
	 *
	 * Four of five blocked is 0.8, and that is the entire difference between this plugin and a single
	 * ray, which can only ever say 0 or 1. Firing no taps means "no opinion", which is 0 - a source that
	 * has never been measured runs open rather than muffled, because a wrongly open sound is a smaller
	 * mistake than a wrongly muted one.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Maths")
	static float EvaluateOcclusion(int32 BlockedTaps, int32 TapsFired);

	/**
	 * The tap pattern for one measurement.
	 *
	 * Tap 0 is always the straight centre line, so the first tap of any pattern is exactly the ray the
	 * engine's own occlusion would have fired - one tap is a strict downgrade to that behaviour, not a
	 * different one. The rest sit on a circle of PatternRadius around the source, perpendicular to the
	 * line, with the listener end pushed the opposite way by a third of that. Crossing the ends is what
	 * makes the pattern sample the opening rather than the wall: a set of parallel rays through a door
	 * frame either all pass or all fail, which is the yes/no answer again with extra cost.
	 *
	 * Deterministic - no random offsets. A jittered pattern would make a still source's occlusion
	 * shimmer between measurements, and the smoothing would then have to hide the plugin's own noise.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Maths")
	static void BuildTapPattern(
		const FVector& SourceLocation,
		const FVector& ListenerLocation,
		int32 TapCount,
		float PatternRadius,
		TArray<FVector>& OutStarts,
		TArray<FVector>& OutEnds);

	/**
	 * How much this source deserves the shared budget, 0..1.
	 *
	 * Loudness times priority times a distance falloff, and nothing else. An inaudible source scores
	 * zero and is left out of the schedule entirely - it keeps its last value and costs nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Maths")
	static float ScoreSource(const FSoundVeilSourceRank& Source);

	/**
	 * Build one full scheduling round: who gets measured, in what order, and who gets extra turns.
	 *
	 * Two properties, and they pull against each other:
	 *
	 *   Nobody starves. Every audible source appears in the round at least once, so the worst wait is
	 *   bounded by the length of the round divided by how many measurements fit in a frame - and that
	 *   is a number the counter box can show. A pure "loudest first" schedule has no such bound; the
	 *   quiet sources at the back are simply never reached.
	 *
	 *   Near and loud are measured more often. A source scoring at the top of the set appears up to
	 *   MaxTurnsPerSource times, spread across the round rather than bunched, so its extra turns are
	 *   actually spaced out in time. When every source scores the same the weights collapse to one
	 *   apiece and this degenerates to plain round robin, which is the right answer for a set of
	 *   identical ambient loops.
	 *
	 * Returns the round length, which is OutRound.Num().
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Maths")
	static int32 RankSources(const TArray<FSoundVeilSourceRank>& Sources, int32 MaxTurnsPerSource, TArray<int32>& OutRound);

	/**
	 * Split a source set into the ones tracked individually and the ones that share a line.
	 *
	 * The tracked set is the top MaxSources by score. What is left is not dropped and not silenced: it
	 * is summarised into one entry, gets one shared occlusion value, and appears in the counter box as a
	 * single collected line. Count plus OutTracked.Num() always equals the number of sources handed in.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Maths")
	static void SelectTrackedSources(
		const TArray<FSoundVeilSourceRank>& Sources,
		int32 MaxSources,
		TArray<int32>& OutTracked,
		FSoundVeilOverflowSummary& OutSummary);

	/**
	 * Volume multiplier and low pass cut-off for an occlusion value, through the profile's two curves.
	 *
	 * Two knobs rather than one switch: volume alone makes a source sound far away, cut-off alone makes
	 * it sound covered, and a wall does both in a proportion that depends on the wall. Passing no
	 * profile falls back to the built-in concrete-ish response so the function is total.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Maths")
	static void ApplyCurves(float Occlusion, const USoundVeilProfile* Profile, float& OutVolumeMultiplier, float& OutLowpassFrequency);

	/**
	 * One frame of attack/release smoothing towards a target occlusion.
	 *
	 * Exponential approach, one time constant for rising and another for falling, and it can never
	 * overshoot: the step is a fraction of the remaining distance, so the result always lands between
	 * where it was and where it is going. That matters more than it sounds - an overshooting smoother
	 * would push a fully occluded source past 1, and the curve lookup past the end of the curve.
	 *
	 * A time of zero snaps. Delta time of zero returns the current value unchanged.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Maths")
	static float SmoothOcclusion(float Current, float Target, float DeltaSeconds, float AttackSeconds, float ReleaseSeconds);

	/**
	 * How many measurements fit in one frame at this budget and tap count.
	 *
	 * Rounded up, because the last measurement of a frame takes whatever taps are left rather than
	 * being skipped: eight traces at five taps is one full measurement plus one three-tap measurement,
	 * not one measurement and three wasted traces.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Maths")
	static int32 EvaluationsPerFrame(int32 TraceBudget, int32 TapCount);

	/**
	 * The longest any source can wait for a measurement, in frames, given a round and a budget.
	 * The counter box shows the measured wait against this number; the measured one stays under it.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Maths")
	static int32 WaitLimitFrames(int32 RoundLength, int32 TraceBudget, int32 TapCount);

	// ==================================================================================================
	// The service, from Blueprint - one call per demo button
	// ==================================================================================================

	/** The occlusion service for this world. Null outside a game or PIE world. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static USoundVeilSubsystem* GetSoundVeil(const UObject* WorldContextObject);

	/** Move the shared per-frame trace budget. Same as SoundVeil.Budget. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void SetTraceBudget(const UObject* WorldContextObject, int32 TracesPerFrame);

	/** Force a tap count on every source, whatever their profiles say. Same as SoundVeil.Taps. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void SetTapCountOverride(const UObject* WorldContextObject, int32 TapCount);

	/** Hand the tap count back to each profile. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void ClearTapCountOverride(const UObject* WorldContextObject);

	/**
	 * The before/after switch. Bypassed, every source runs at its own volume with the filter off while
	 * the service keeps measuring, so the counter box still shows what it would have done.
	 * Same as SoundVeil.Bypass.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void SetBypass(const UObject* WorldContextObject, bool bBypass);

	/** Stop measuring and hold every current value. For screenshots and for pause menus. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void SetFrozen(const UObject* WorldContextObject, bool bFrozen);

	/** Show or hide the counter box. Same as SoundVeil.Show. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static void SetShowStats(const UObject* WorldContextObject, bool bShow);

	/** What the service did on the most recent frame. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil", meta = (WorldContext = "WorldContextObject"))
	static FSoundVeilStats GetStats(const UObject* WorldContextObject);
};
