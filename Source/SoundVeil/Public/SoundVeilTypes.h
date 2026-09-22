// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SoundVeilTypes.generated.h"

/**
 * Distance in cm at which a source counts as "half as interesting" for scheduling purposes.
 *
 * It is a scheduling constant, not an audio one: it only decides who gets a trace first, never how
 * loud anything ends up. Attenuation stays the sound asset's business.
 */
namespace SoundVeil
{
	static constexpr float RankReferenceDistance = 1500.0f;
}

/**
 * Opaque reference to one registered source.
 *
 * Slot plus Serial, not a pointer: slots are recycled, and the serial catches the case where the
 * caller kept a handle to a source that has long since been unregistered and its slot handed to
 * somebody else. An invalid handle is safe to pass anywhere in the API.
 */
USTRUCT(BlueprintType)
struct SOUNDVEIL_API FSoundVeilSourceHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 Slot = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 Serial = 0;

	bool IsValid() const { return Slot != INDEX_NONE && Serial != 0; }

	void Reset()
	{
		Slot = INDEX_NONE;
		Serial = 0;
	}

	bool operator==(const FSoundVeilSourceHandle& Other) const
	{
		return Slot == Other.Slot && Serial == Other.Serial;
	}
};

/**
 * Everything the scheduler needs to know about a source, and nothing else.
 *
 * A plain struct on purpose: the ranking is pure arithmetic over these numbers, so it can be tested
 * without a world, a level or a single audio device. The subsystem fills one of these per source per
 * frame and hands the array to USoundVeilStatics::BuildSchedule.
 */
USTRUCT(BlueprintType)
struct SOUNDVEIL_API FSoundVeilSourceRank
{
	GENERATED_BODY()

	/** Base volume of the source, before occlusion, 0..1. A quiet source is worth less trace budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	float Loudness = 1.0f;

	/** Distance from the listener in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	float Distance = 0.0f;

	/** Profile priority, 0..1. A hand-placed story sound can outrank an ambient loop next to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	float Priority = 1.0f;

	/** Frames since this source last had its occlusion measured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	int32 FramesSinceUpdate = 0;

	/** A source that is not playing, or is muted, or is out of range costs no trace budget at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SoundVeil")
	bool bAudible = true;
};

/** What is left over once the tracked set is full: one line, never a dropped source. */
USTRUCT(BlueprintType)
struct SOUNDVEIL_API FSoundVeilOverflowSummary
{
	GENERATED_BODY()

	/** How many sources are in the grouped set. Zero means the tracked set was never full. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 Count = 0;

	/** Loudness of the loudest grouped source, so the counter box can say how much is being lumped. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float LoudestLoudness = 0.0f;

	/** Mean distance of the grouped sources in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float MeanDistance = 0.0f;

	/** Occlusion the whole group shares, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float Occlusion = 0.0f;
};

/** A source as the counter box and Blueprint see it. */
USTRUCT(BlueprintType)
struct SOUNDVEIL_API FSoundVeilSourceState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	FSoundVeilSourceHandle Handle;

	/** Actor that owns the source, for the counter box label. May be null for a detached source. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	TWeakObjectPtr<AActor> Owner;

	/** Smoothed occlusion, 0 = clear line, 1 = fully covered. This is the value everything else uses. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float Occlusion = 0.0f;

	/** Raw occlusion from the last measurement, before attack/release. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float TargetOcclusion = 0.0f;

	/** Volume multiplier the occlusion curve produced, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float VolumeMultiplier = 1.0f;

	/** Low pass cut-off the occlusion curve produced, in Hz. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float LowpassFrequency = 20000.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float Loudness = 0.0f;

	/** Frames since this source last had a measurement. The budget made visible, per source. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 FramesSinceUpdate = 0;

	/** Taps fired on the last measurement. Below the profile's TapCount when the budget ran short. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 LastTapCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	bool bAudible = true;

	/** True while this source is in the grouped set rather than tracked individually. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	bool bGrouped = false;
};

/** What the service did on the most recent frame. Everything in the counter box comes from here. */
USTRUCT(BlueprintType)
struct SOUNDVEIL_API FSoundVeilStats
{
	GENERATED_BODY()

	/** Traces started this frame. Never above TraceBudget - that is the whole point of the plugin. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 TracesThisFrame = 0;

	/** The hard cap those traces were counted against. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 TraceBudget = 8;

	/** Traces a naive one-ray-per-source implementation would have fired instead. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 NaiveTraces = 0;

	/** Sources that got a measurement this frame. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 EvaluationsThisFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 Sources = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 AudibleSources = 0;

	/** Sources in the grouped set because the tracked set was full. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 GroupedSources = 0;

	/** The worst wait in the world this frame, in frames. The number the budget is really about. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 LongestWaitFrames = 0;

	/** Wait that the current budget, tap count and source count imply. LongestWaitFrames stays under it. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 WaitLimitFrames = 0;

	/** Length of the current scheduling round, counting the extra turns loud near sources earn. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	int32 RoundLength = 0;

	/** Milliseconds spent in the service's own work this frame, traces excluded (they are async). */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float UpdateMs = 0.0f;

	/** Mean occlusion over the audible sources, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	float MeanOcclusion = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	bool bBypassed = false;

	UPROPERTY(BlueprintReadOnly, Category = "SoundVeil")
	bool bFrozen = false;
};
