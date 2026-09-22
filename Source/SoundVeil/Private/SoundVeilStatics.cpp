// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilStatics.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SoundVeilProfile.h"
#include "SoundVeilSubsystem.h"

// -------------------------------------------------------------------------------------------------------
// Pure maths
// -------------------------------------------------------------------------------------------------------

float USoundVeilStatics::EvaluateOcclusion(int32 BlockedTaps, int32 TapsFired)
{
	if (TapsFired <= 0)
	{
		return 0.0f;
	}

	const int32 Blocked = FMath::Clamp(BlockedTaps, 0, TapsFired);
	return static_cast<float>(Blocked) / static_cast<float>(TapsFired);
}

void USoundVeilStatics::BuildTapPattern(
	const FVector& SourceLocation,
	const FVector& ListenerLocation,
	int32 TapCount,
	float PatternRadius,
	TArray<FVector>& OutStarts,
	TArray<FVector>& OutEnds)
{
	OutStarts.Reset();
	OutEnds.Reset();

	const int32 Taps = FMath::Max(TapCount, 1);
	OutStarts.Reserve(Taps);
	OutEnds.Reserve(Taps);

	// Tap 0 is the plain centre line - the same ray the engine's own occlusion fires.
	OutStarts.Add(SourceLocation);
	OutEnds.Add(ListenerLocation);

	if (Taps == 1 || PatternRadius <= KINDA_SMALL_NUMBER)
	{
		for (int32 Index = 1; Index < Taps; ++Index)
		{
			OutStarts.Add(SourceLocation);
			OutEnds.Add(ListenerLocation);
		}
		return;
	}

	FVector Direction = ListenerLocation - SourceLocation;
	if (!Direction.Normalize())
	{
		// Source and listener in the same spot: any basis will do, and the answer will be "open"
		// anyway because the profile's minimum distance catches this case before we ever get here.
		Direction = FVector::ForwardVector;
	}

	FVector Right = FVector::CrossProduct(Direction, FVector::UpVector);
	if (!Right.Normalize())
	{
		Right = FVector::CrossProduct(Direction, FVector::ForwardVector).GetSafeNormal();
	}
	const FVector Up = FVector::CrossProduct(Right, Direction).GetSafeNormal();

	const int32 RingTaps = Taps - 1;

	// The listener end moves the opposite way and by less: a doorway is wide, a head is not, and the
	// point of crossing the ends is that the rays fan through the opening instead of running parallel.
	constexpr float ListenerRadiusScale = 0.35f;

	for (int32 Index = 0; Index < RingTaps; ++Index)
	{
		const float Angle = (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(RingTaps);
		const FVector Offset = (FMath::Cos(Angle) * Right + FMath::Sin(Angle) * Up) * PatternRadius;

		OutStarts.Add(SourceLocation + Offset);
		OutEnds.Add(ListenerLocation - Offset * ListenerRadiusScale);
	}
}

float USoundVeilStatics::ScoreSource(const FSoundVeilSourceRank& Source)
{
	if (!Source.bAudible)
	{
		return 0.0f;
	}

	const float Loudness = FMath::Clamp(Source.Loudness, 0.0f, 1.0f);
	const float Priority = FMath::Clamp(Source.Priority, 0.0f, 1.0f);
	const float Distance = FMath::Max(Source.Distance, 0.0f);
	const float Falloff = SoundVeil::RankReferenceDistance / (SoundVeil::RankReferenceDistance + Distance);

	return Loudness * Priority * Falloff;
}

int32 USoundVeilStatics::RankSources(const TArray<FSoundVeilSourceRank>& Sources, int32 MaxTurnsPerSource, TArray<int32>& OutRound)
{
	OutRound.Reset();

	const int32 MaxTurns = FMath::Clamp(MaxTurnsPerSource, 1, 8);

	struct FEntry
	{
		int32 Index = 0;
		float Score = 0.0f;
		int32 Turns = 1;
	};

	TArray<FEntry> Entries;
	Entries.Reserve(Sources.Num());

	float MaxScore = 0.0f;
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const FSoundVeilSourceRank& Source = Sources[Index];
		if (!Source.bAudible)
		{
			// Not skipped out of thrift but out of correctness: measuring something nobody can hear and
			// then handing the budget to a source the player is standing next to is the wrong trade.
			continue;
		}

		FEntry Entry;
		Entry.Index = Index;
		Entry.Score = ScoreSource(Source);
		MaxScore = FMath::Max(MaxScore, Entry.Score);
		Entries.Add(Entry);
	}

	if (Entries.Num() == 0)
	{
		return 0;
	}

	// Loudest and nearest first, and a stable tie-break on the index so the same set always produces
	// the same round - a schedule that reshuffles on ties makes a wait measurement meaningless.
	Entries.Sort([](const FEntry& A, const FEntry& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		return A.Index < B.Index;
	});

	int32 MinTurns = MaxTurns;
	int32 MaxAssignedTurns = 1;
	if (MaxScore > KINDA_SMALL_NUMBER)
	{
		for (FEntry& Entry : Entries)
		{
			const float Relative = Entry.Score / MaxScore;
			Entry.Turns = FMath::Clamp(FMath::RoundToInt(Relative * static_cast<float>(MaxTurns)), 1, MaxTurns);
			MinTurns = FMath::Min(MinTurns, Entry.Turns);
			MaxAssignedTurns = FMath::Max(MaxAssignedTurns, Entry.Turns);
		}
	}
	else
	{
		MinTurns = 1;
	}

	if (MinTurns == MaxAssignedTurns)
	{
		// Everybody scored the same band, so nobody is more urgent than anybody else. Collapsing to one
		// turn apiece here is what makes forty identical ambient loops behave as plain round robin, with
		// the shortest possible round and therefore the shortest possible worst-case wait.
		for (FEntry& Entry : Entries)
		{
			Entry.Turns = 1;
		}
		MaxAssignedTurns = 1;
	}

	// Emitted pass by pass rather than source by source: a source with three turns lands at the start,
	// the middle and the end of the round instead of three frames in a row, which is where the extra
	// turns actually buy something.
	OutRound.Reserve(Entries.Num() * MaxAssignedTurns);
	for (int32 Pass = 0; Pass < MaxAssignedTurns; ++Pass)
	{
		for (const FEntry& Entry : Entries)
		{
			if (Entry.Turns > Pass)
			{
				OutRound.Add(Entry.Index);
			}
		}
	}

	return OutRound.Num();
}

void USoundVeilStatics::SelectTrackedSources(
	const TArray<FSoundVeilSourceRank>& Sources,
	int32 MaxSources,
	TArray<int32>& OutTracked,
	FSoundVeilOverflowSummary& OutSummary)
{
	OutTracked.Reset();
	OutSummary = FSoundVeilOverflowSummary();

	const int32 Limit = FMath::Max(MaxSources, 1);

	if (Sources.Num() <= Limit)
	{
		OutTracked.Reserve(Sources.Num());
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			OutTracked.Add(Index);
		}
		return;
	}

	TArray<int32> Order;
	Order.Reserve(Sources.Num());
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		Order.Add(Index);
	}

	Order.Sort([&Sources](int32 A, int32 B)
	{
		const float ScoreA = ScoreSource(Sources[A]);
		const float ScoreB = ScoreSource(Sources[B]);
		if (ScoreA != ScoreB)
		{
			return ScoreA > ScoreB;
		}
		return A < B;
	});

	OutTracked.Append(Order.GetData(), Limit);
	OutTracked.Sort();

	// Everything past the limit becomes exactly one line. Not one line per source that did not fit, and
	// not silence: the group shares a value and keeps playing.
	double DistanceSum = 0.0;
	for (int32 Index = Limit; Index < Order.Num(); ++Index)
	{
		const FSoundVeilSourceRank& Source = Sources[Order[Index]];
		OutSummary.Count++;
		OutSummary.LoudestLoudness = FMath::Max(OutSummary.LoudestLoudness, FMath::Clamp(Source.Loudness, 0.0f, 1.0f));
		DistanceSum += FMath::Max(Source.Distance, 0.0f);
	}

	if (OutSummary.Count > 0)
	{
		OutSummary.MeanDistance = static_cast<float>(DistanceSum / static_cast<double>(OutSummary.Count));
	}
}

void USoundVeilStatics::ApplyCurves(float Occlusion, const USoundVeilProfile* Profile, float& OutVolumeMultiplier, float& OutLowpassFrequency)
{
	const float Clamped = FMath::Clamp(Occlusion, 0.0f, 1.0f);

	if (Profile)
	{
		OutVolumeMultiplier = Profile->EvaluateVolume(Clamped);
		OutLowpassFrequency = Profile->EvaluateLowpass(Clamped);
		return;
	}

	// No profile: the built-in response, which is roughly a plastered wall. Deliberately not silence at
	// full occlusion - a source you can no longer hear at all reads as a bug, not as a wall.
	OutVolumeMultiplier = FMath::Clamp(FMath::Lerp(1.0f, 0.25f, Clamped), 0.0f, 1.0f);
	OutLowpassFrequency = FMath::Clamp(
		FMath::Lerp(USoundVeilProfile::MaxLowpassFrequency, 700.0f, Clamped),
		USoundVeilProfile::MinLowpassFrequency,
		USoundVeilProfile::MaxLowpassFrequency);
}

float USoundVeilStatics::SmoothOcclusion(float Current, float Target, float DeltaSeconds, float AttackSeconds, float ReleaseSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return Current;
	}

	const bool bRising = Target > Current;
	const float TimeConstant = FMath::Max(bRising ? AttackSeconds : ReleaseSeconds, 0.0f);

	if (TimeConstant <= KINDA_SMALL_NUMBER)
	{
		return Target;
	}

	// 1 - e^-t/tau, which is a fraction strictly inside 0..1 for any positive delta. That is the whole
	// no-overshoot argument: the step can never be larger than the distance left to travel.
	const float Alpha = 1.0f - FMath::Exp(-DeltaSeconds / TimeConstant);
	return Current + (Target - Current) * FMath::Clamp(Alpha, 0.0f, 1.0f);
}

int32 USoundVeilStatics::EvaluationsPerFrame(int32 TraceBudget, int32 TapCount)
{
	if (TraceBudget <= 0)
	{
		return 0;
	}

	return FMath::DivideAndRoundUp(TraceBudget, FMath::Max(TapCount, 1));
}

int32 USoundVeilStatics::WaitLimitFrames(int32 RoundLength, int32 TraceBudget, int32 TapCount)
{
	if (RoundLength <= 0)
	{
		return 0;
	}

	const int32 PerFrame = EvaluationsPerFrame(TraceBudget, TapCount);
	if (PerFrame <= 0)
	{
		// A budget of zero means nothing is ever measured. Reported as the round length rather than as
		// infinity so the counter box has a number to print.
		return RoundLength;
	}

	return FMath::DivideAndRoundUp(RoundLength, PerFrame);
}

// -------------------------------------------------------------------------------------------------------
// The service, from Blueprint
// -------------------------------------------------------------------------------------------------------

USoundVeilSubsystem* USoundVeilStatics::GetSoundVeil(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<USoundVeilSubsystem>() : nullptr;
}

void USoundVeilStatics::SetTraceBudget(const UObject* WorldContextObject, int32 TracesPerFrame)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->SetBudget(TracesPerFrame);
	}
}

void USoundVeilStatics::SetTapCountOverride(const UObject* WorldContextObject, int32 TapCount)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->SetTapCountOverride(TapCount);
	}
}

void USoundVeilStatics::ClearTapCountOverride(const UObject* WorldContextObject)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->ClearTapCountOverride();
	}
}

void USoundVeilStatics::SetBypass(const UObject* WorldContextObject, bool bBypass)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->SetBypass(bBypass);
	}
}

void USoundVeilStatics::SetFrozen(const UObject* WorldContextObject, bool bFrozen)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->Freeze(bFrozen);
	}
}

void USoundVeilStatics::SetShowStats(const UObject* WorldContextObject, bool bShow)
{
	if (USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		Service->SetShowStats(bShow);
	}
}

FSoundVeilStats USoundVeilStatics::GetStats(const UObject* WorldContextObject)
{
	if (const USoundVeilSubsystem* Service = GetSoundVeil(WorldContextObject))
	{
		return Service->GetStats();
	}

	return FSoundVeilStats();
}
