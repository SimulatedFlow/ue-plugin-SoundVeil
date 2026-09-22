// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "SoundVeilProfile.h"
#include "SoundVeilStatics.h"
#include "SoundVeilTypes.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SoundVeilTests
{
	// CommandletContext as well as EditorContext: the four things tested here are the four places the
	// plugin can be quietly wrong, and a test that only runs when somebody has the editor open is a
	// test that will not be there when it matters.
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::CommandletContext
		| EAutomationTestFlags::EngineFilter;

	/** A source description for the ranking maths. Nothing here needs a world or an audio device. */
	FSoundVeilSourceRank MakeRank(float Loudness, float Distance, float Priority = 1.0f, bool bAudible = true)
	{
		FSoundVeilSourceRank Rank;
		Rank.Loudness = Loudness;
		Rank.Distance = Distance;
		Rank.Priority = Priority;
		Rank.bAudible = bAudible;
		return Rank;
	}

	/** A profile held against GC for the life of a test, with the built-in curves. */
	TStrongObjectPtr<USoundVeilProfile> MakeProfile()
	{
		return TStrongObjectPtr<USoundVeilProfile>(NewObject<USoundVeilProfile>(GetTransientPackage()));
	}
}

//
// (1) Four blocked taps out of five is 0.8 - the whole difference between this plugin and one ray.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilEvaluateOcclusionTest,
	"SoundVeil.Maths.EvaluateOcclusionIsAFraction",
	SoundVeilTests::TestFlags)

bool FSoundVeilEvaluateOcclusionTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("4 of 5 blocked"), USoundVeilStatics::EvaluateOcclusion(4, 5), 0.8f);
	TestEqual(TEXT("0 of 5 blocked"), USoundVeilStatics::EvaluateOcclusion(0, 5), 0.0f);
	TestEqual(TEXT("5 of 5 blocked"), USoundVeilStatics::EvaluateOcclusion(5, 5), 1.0f);
	TestEqual(TEXT("2 of 5 blocked"), USoundVeilStatics::EvaluateOcclusion(2, 5), 0.4f);

	// One tap is the engine's own answer: it can only be 0 or 1, and it should be exactly that.
	TestEqual(TEXT("1 of 1 blocked"), USoundVeilStatics::EvaluateOcclusion(1, 1), 1.0f);
	TestEqual(TEXT("0 of 1 blocked"), USoundVeilStatics::EvaluateOcclusion(0, 1), 0.0f);

	// A measurement that never happened must read as open, not as covered: a wrongly open sound is a
	// smaller mistake than a wrongly muted one.
	TestEqual(TEXT("no taps fired"), USoundVeilStatics::EvaluateOcclusion(3, 0), 0.0f);
	TestEqual(TEXT("negative taps"), USoundVeilStatics::EvaluateOcclusion(3, -2), 0.0f);

	// Nonsense in, clamped out - never above 1, which would run the curve lookup off its end.
	TestEqual(TEXT("more blocked than fired"), USoundVeilStatics::EvaluateOcclusion(9, 5), 1.0f);
	TestEqual(TEXT("negative blocked"), USoundVeilStatics::EvaluateOcclusion(-3, 5), 0.0f);

	return true;
}

//
// (2) The schedule prefers the near and the loud, and gives them more turns.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilRankSourcesTest,
	"SoundVeil.Schedule.RanksNearAndLoudFirst",
	SoundVeilTests::TestFlags)

bool FSoundVeilRankSourcesTest::RunTest(const FString& Parameters)
{
	using namespace SoundVeilTests;

	TArray<FSoundVeilSourceRank> Sources;
	Sources.Add(MakeRank(1.0f, 100.0f));   // 0: near and loud
	Sources.Add(MakeRank(1.0f, 6000.0f));  // 1: loud but far
	Sources.Add(MakeRank(0.2f, 100.0f));   // 2: near but quiet

	TestTrue(TEXT("near beats far"), USoundVeilStatics::ScoreSource(Sources[0]) > USoundVeilStatics::ScoreSource(Sources[1]));
	TestTrue(TEXT("loud beats quiet"), USoundVeilStatics::ScoreSource(Sources[0]) > USoundVeilStatics::ScoreSource(Sources[2]));

	TArray<int32> Round;
	const int32 RoundLength = USoundVeilStatics::RankSources(Sources, 3, Round);

	TestEqual(TEXT("round length is what was returned"), RoundLength, Round.Num());
	TestTrue(TEXT("round is not empty"), Round.Num() >= 3);
	TestEqual(TEXT("the near loud source is measured first"), Round[0], 0);

	int32 Turns[3] = { 0, 0, 0 };
	for (int32 Index : Round)
	{
		TestTrue(TEXT("round only contains real indices"), Sources.IsValidIndex(Index));
		Turns[Index]++;
	}

	TestTrue(TEXT("near and loud gets more turns than far"), Turns[0] > Turns[1]);
	TestTrue(TEXT("near and loud gets more turns than quiet"), Turns[0] > Turns[2]);

	// Nobody is left out, however badly they score. This is the half of the schedule that stops the
	// quiet sources at the back from being starved by the loud ones at the front.
	TestTrue(TEXT("the far source still gets a turn"), Turns[1] >= 1);
	TestTrue(TEXT("the quiet source still gets a turn"), Turns[2] >= 1);

	// An inaudible source is not scheduled at all - it keeps its last value and costs nothing.
	Sources.Add(MakeRank(1.0f, 100.0f, 1.0f, /*bAudible=*/false));
	Round.Reset();
	USoundVeilStatics::RankSources(Sources, 3, Round);
	TestFalse(TEXT("an inaudible source is not in the round"), Round.Contains(3));

	// Priority is the tie-breaker for two sources the distance and loudness rules cannot separate.
	TArray<FSoundVeilSourceRank> Tied;
	Tied.Add(MakeRank(1.0f, 500.0f, 0.25f));
	Tied.Add(MakeRank(1.0f, 500.0f, 1.0f));
	Round.Reset();
	USoundVeilStatics::RankSources(Tied, 3, Round);
	TestEqual(TEXT("the higher priority source is measured first"), Round[0], 1);

	return true;
}

//
// (3) Nobody starves: no source waits longer than the round divided by what fits in a frame.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilStarvationTest,
	"SoundVeil.Schedule.NoSourceWaitsLongerThanTheLimit",
	SoundVeilTests::TestFlags)

bool FSoundVeilStarvationTest::RunTest(const FString& Parameters)
{
	using namespace SoundVeilTests;

	// The scheduler, run dry: no world, no traces, just the queue and the budget. Every frame the wait
	// of every source goes up by one, then the frame's ration of measurements resets the ones it
	// reaches - which is exactly the order the subsystem's tick does it in.
	auto Simulate = [this](const TArray<FSoundVeilSourceRank>& Sources, int32 Budget, int32 Taps, int32 MaxTurns, int32 Frames)
	{
		TArray<int32> Round;
		USoundVeilStatics::RankSources(Sources, MaxTurns, Round);

		const int32 PerFrame = USoundVeilStatics::EvaluationsPerFrame(Budget, Taps);
		const int32 Limit = USoundVeilStatics::WaitLimitFrames(Round.Num(), Budget, Taps);

		TArray<int32> Waits;
		Waits.Init(0, Sources.Num());

		int32 Cursor = 0;
		int32 WorstWait = 0;

		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			for (int32& Wait : Waits)
			{
				++Wait;
			}

			for (int32 Index = 0; Index < Sources.Num(); ++Index)
			{
				if (Sources[Index].bAudible)
				{
					WorstWait = FMath::Max(WorstWait, Waits[Index]);
				}
			}

			for (int32 Served = 0; Served < PerFrame && Round.Num() > 0; ++Served)
			{
				if (Cursor >= Round.Num())
				{
					Cursor = 0;
				}
				Waits[Round[Cursor++]] = 0;
			}
		}

		return TPair<int32, int32>(WorstWait, Limit);
	};

	// Forty identical ambient loops, the demo's case. Identical scores collapse the weights to one
	// apiece, so the round is exactly the source count and the bound is exactly ceil(N / per frame).
	TArray<FSoundVeilSourceRank> Uniform;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Uniform.Add(MakeRank(1.0f, 1000.0f));
	}

	{
		TArray<int32> Round;
		const int32 RoundLength = USoundVeilStatics::RankSources(Uniform, 3, Round);
		TestEqual(TEXT("identical sources make a round of exactly N"), RoundLength, 40);

		const int32 PerFrame = USoundVeilStatics::EvaluationsPerFrame(8, 5);
		TestEqual(TEXT("eight traces at five taps is two measurements"), PerFrame, 2);
		TestEqual(TEXT("the wait limit is ceil(N / per frame)"), USoundVeilStatics::WaitLimitFrames(40, 8, 5), 20);
	}

	{
		const TPair<int32, int32> Result = Simulate(Uniform, 8, 5, 3, 400);
		TestTrue(
			FString::Printf(TEXT("budget 8: worst wait %d must not exceed the limit %d"), Result.Key, Result.Value),
			Result.Key <= Result.Value);
	}

	{
		// The demo's other button: the same forty sources at a budget of 64. The limit collapses, and
		// that collapse is the number the counter box is showing off.
		const TPair<int32, int32> Result = Simulate(Uniform, 64, 5, 3, 400);
		TestTrue(
			FString::Printf(TEXT("budget 64: worst wait %d must not exceed the limit %d"), Result.Key, Result.Value),
			Result.Key <= Result.Value);
		TestTrue(TEXT("a bigger budget means a shorter wait"), Result.Value < 20);
	}

	{
		// A budget of one, which is less than one full measurement: the measurement degrades to a
		// single tap rather than never happening, so the bound still holds.
		const TPair<int32, int32> Result = Simulate(Uniform, 1, 5, 3, 400);
		TestEqual(TEXT("a budget below one pattern still measures once"), USoundVeilStatics::EvaluationsPerFrame(1, 5), 1);
		TestTrue(
			FString::Printf(TEXT("budget 1: worst wait %d must not exceed the limit %d"), Result.Key, Result.Value),
			Result.Key <= Result.Value);
	}

	{
		// Wildly mixed sources: the loud near ones take extra turns, which makes the round longer - and
		// the bound is stated over the round length, so it still holds for the quiet ones at the back.
		TArray<FSoundVeilSourceRank> Mixed;
		for (int32 Index = 0; Index < 40; ++Index)
		{
			Mixed.Add(MakeRank(1.0f - Index * 0.02f, 200.0f + Index * 400.0f));
		}

		TArray<int32> Round;
		USoundVeilStatics::RankSources(Mixed, 3, Round);

		for (int32 Index = 0; Index < Mixed.Num(); ++Index)
		{
			TestTrue(
				FString::Printf(TEXT("source %d appears in the round at least once"), Index),
				Round.Contains(Index));
		}

		const TPair<int32, int32> Result = Simulate(Mixed, 8, 5, 3, 600);
		TestTrue(
			FString::Printf(TEXT("mixed: worst wait %d must not exceed the limit %d"), Result.Key, Result.Value),
			Result.Key <= Result.Value);
	}

	return true;
}

//
// (4) The curves are obeyed and clamped at both ends.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilApplyCurvesTest,
	"SoundVeil.Maths.ApplyCurvesFollowsTheProfile",
	SoundVeilTests::TestFlags)

bool FSoundVeilApplyCurvesTest::RunTest(const FString& Parameters)
{
	using namespace SoundVeilTests;

	TStrongObjectPtr<USoundVeilProfile> Profile = MakeProfile();

	float Volume = 0.0f;
	float Lowpass = 0.0f;

	USoundVeilStatics::ApplyCurves(0.0f, Profile.Get(), Volume, Lowpass);
	TestEqual(TEXT("an open source keeps its volume"), Volume, 1.0f, 0.001f);
	TestEqual(TEXT("an open source is unfiltered"), Lowpass, USoundVeilProfile::MaxLowpassFrequency, 1.0f);

	USoundVeilStatics::ApplyCurves(1.0f, Profile.Get(), Volume, Lowpass);
	TestEqual(TEXT("a covered source is damped by the curve"), Volume, 0.25f, 0.001f);
	TestEqual(TEXT("a covered source loses its top end"), Lowpass, 700.0f, 1.0f);

	// The half open door: strictly between the two ends on both knobs. If this ever comes back equal
	// to one of the ends, the plugin has quietly become a yes/no switch again.
	float MidVolume = 0.0f;
	float MidLowpass = 0.0f;
	USoundVeilStatics::ApplyCurves(0.5f, Profile.Get(), MidVolume, MidLowpass);
	TestTrue(TEXT("half occlusion is between the ends on volume"), MidVolume < 1.0f && MidVolume > 0.25f);
	TestTrue(TEXT("half occlusion is between the ends on cut-off"), MidLowpass < USoundVeilProfile::MaxLowpassFrequency && MidLowpass > 700.0f);

	// Out of range in, in range out.
	float LowVolume = 0.0f;
	float LowLowpass = 0.0f;
	USoundVeilStatics::ApplyCurves(-3.0f, Profile.Get(), LowVolume, LowLowpass);
	TestEqual(TEXT("negative occlusion clamps to open"), LowVolume, 1.0f, 0.001f);

	float HighVolume = 0.0f;
	float HighLowpass = 0.0f;
	USoundVeilStatics::ApplyCurves(4.0f, Profile.Get(), HighVolume, HighLowpass);
	TestEqual(TEXT("occlusion above one clamps to covered"), HighVolume, 0.25f, 0.001f);
	TestEqual(TEXT("cut-off stays above the floor"), HighLowpass, 700.0f, 1.0f);

	// A curve that has been dragged somewhere silly must not produce a negative volume or a cut-off
	// the filter cannot take.
	FRichCurve& VolumeCurve = *Profile->OcclusionToVolume.GetRichCurve();
	VolumeCurve.Reset();
	VolumeCurve.AddKey(0.0f, 40.0f);
	VolumeCurve.AddKey(1.0f, -12.0f);

	FRichCurve& LowpassCurve = *Profile->OcclusionToLowpass.GetRichCurve();
	LowpassCurve.Reset();
	LowpassCurve.AddKey(0.0f, 999999.0f);
	LowpassCurve.AddKey(1.0f, -50.0f);

	USoundVeilStatics::ApplyCurves(0.0f, Profile.Get(), Volume, Lowpass);
	TestEqual(TEXT("volume is clamped to one"), Volume, 1.0f, 0.001f);
	TestEqual(TEXT("cut-off is clamped to the ceiling"), Lowpass, USoundVeilProfile::MaxLowpassFrequency, 0.001f);

	USoundVeilStatics::ApplyCurves(1.0f, Profile.Get(), Volume, Lowpass);
	TestEqual(TEXT("volume is clamped to zero"), Volume, 0.0f, 0.001f);
	TestEqual(TEXT("cut-off is clamped to the floor"), Lowpass, USoundVeilProfile::MinLowpassFrequency, 0.001f);

	// No profile at all still gives a usable answer, so nothing has to null-check before asking.
	USoundVeilStatics::ApplyCurves(0.0f, nullptr, Volume, Lowpass);
	TestEqual(TEXT("no profile, open"), Volume, 1.0f, 0.001f);
	USoundVeilStatics::ApplyCurves(1.0f, nullptr, Volume, Lowpass);
	TestTrue(TEXT("no profile, covered, still audible"), Volume > 0.0f && Volume < 1.0f);

	return true;
}

//
// (5) Attack and release approach the target monotonically and never overshoot it.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilSmoothingTest,
	"SoundVeil.Maths.SmoothingIsMonotoneAndDoesNotOvershoot",
	SoundVeilTests::TestFlags)

bool FSoundVeilSmoothingTest::RunTest(const FString& Parameters)
{
	constexpr float Attack = 0.15f;
	constexpr float Release = 0.35f;
	constexpr float Step = 1.0f / 60.0f;

	// Rising towards a fully covered measurement.
	float Value = 0.0f;
	float Previous = -1.0f;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Value = USoundVeilStatics::SmoothOcclusion(Value, 1.0f, Step, Attack, Release);

		TestTrue(TEXT("rising never overshoots the target"), Value <= 1.0f);
		TestTrue(TEXT("rising is monotone"), Value >= Previous);
		Previous = Value;
	}
	TestTrue(TEXT("rising gets there within two seconds"), Value > 0.99f);

	// And falling back to open again.
	Previous = 2.0f;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Value = USoundVeilStatics::SmoothOcclusion(Value, 0.0f, Step, Attack, Release);

		TestTrue(TEXT("falling never undershoots the target"), Value >= 0.0f);
		TestTrue(TEXT("falling is monotone"), Value <= Previous);
		Previous = Value;
	}
	TestTrue(TEXT("falling gets there within four seconds"), Value < 0.01f);

	// The asymmetry is the point: a shorter attack than release means a door closing is heard at once
	// and somebody walking through the line of sight is not heard at all.
	const float Up = USoundVeilStatics::SmoothOcclusion(0.5f, 1.0f, Step, Attack, Release) - 0.5f;
	const float Down = 0.5f - USoundVeilStatics::SmoothOcclusion(0.5f, 0.0f, Step, Attack, Release);
	TestTrue(TEXT("attack is faster than release"), Up > Down);

	// Degenerate inputs.
	TestEqual(TEXT("no time passed, no change"), USoundVeilStatics::SmoothOcclusion(0.3f, 1.0f, 0.0f, Attack, Release), 0.3f);
	TestEqual(TEXT("a zero attack snaps"), USoundVeilStatics::SmoothOcclusion(0.3f, 1.0f, Step, 0.0f, Release), 1.0f);
	TestEqual(TEXT("a zero release snaps"), USoundVeilStatics::SmoothOcclusion(0.3f, 0.0f, Step, Attack, 0.0f), 0.0f);
	TestEqual(TEXT("already there, stays there"), USoundVeilStatics::SmoothOcclusion(0.7f, 0.7f, Step, Attack, Release), 0.7f);

	// A huge frame - an editor hitch, a level load - must not fling the value past the target.
	const float AfterHitch = USoundVeilStatics::SmoothOcclusion(0.0f, 1.0f, 30.0f, Attack, Release);
	TestTrue(TEXT("a thirty second frame does not overshoot"), AfterHitch <= 1.0f);

	return true;
}

//
// (6) Past Max Sources the extra sources become exactly one collected line, and none is lost.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilOverflowTest,
	"SoundVeil.Schedule.OverflowBecomesOneLineAndLosesNothing",
	SoundVeilTests::TestFlags)

bool FSoundVeilOverflowTest::RunTest(const FString& Parameters)
{
	using namespace SoundVeilTests;

	constexpr int32 SourceCount = 200;
	constexpr int32 MaxSources = 128;

	TArray<FSoundVeilSourceRank> Sources;
	for (int32 Index = 0; Index < SourceCount; ++Index)
	{
		// Descending loudness, so the tracked set should be exactly the first 128 by index.
		Sources.Add(MakeRank(1.0f - Index * 0.004f, 1000.0f));
	}

	TArray<int32> Tracked;
	FSoundVeilOverflowSummary Summary;
	USoundVeilStatics::SelectTrackedSources(Sources, MaxSources, Tracked, Summary);

	TestEqual(TEXT("the tracked set is exactly Max Sources"), Tracked.Num(), MaxSources);
	TestEqual(TEXT("the rest are counted in one summary"), Summary.Count, SourceCount - MaxSources);
	TestEqual(TEXT("nothing is lost"), Tracked.Num() + Summary.Count, SourceCount);

	// Every index appears exactly once, either tracked or grouped. The grouped ones are still playing;
	// they just share a value.
	TSet<int32> Seen;
	for (int32 Index : Tracked)
	{
		TestTrue(TEXT("tracked indices are real"), Sources.IsValidIndex(Index));
		TestFalse(TEXT("no source is tracked twice"), Seen.Contains(Index));
		Seen.Add(Index);
	}
	TestEqual(TEXT("the tracked set has no duplicates"), Seen.Num(), MaxSources);

	// The loudest are the ones that keep their own value.
	TestTrue(TEXT("the loudest source is tracked"), Seen.Contains(0));
	TestFalse(TEXT("the quietest source is grouped"), Seen.Contains(SourceCount - 1));

	TestTrue(TEXT("the summary reports the loudest grouped source"), Summary.LoudestLoudness > 0.0f);
	TestTrue(TEXT("the summary reports a distance"), Summary.MeanDistance > 0.0f);

	// Under the limit there is no summary line at all.
	TArray<FSoundVeilSourceRank> Few;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Few.Add(MakeRank(1.0f, 1000.0f));
	}

	Tracked.Reset();
	USoundVeilStatics::SelectTrackedSources(Few, MaxSources, Tracked, Summary);
	TestEqual(TEXT("everything fits"), Tracked.Num(), 10);
	TestEqual(TEXT("no grouped line when everything fits"), Summary.Count, 0);

	// Exactly at the limit is still not an overflow.
	Tracked.Reset();
	USoundVeilStatics::SelectTrackedSources(Few, 10, Tracked, Summary);
	TestEqual(TEXT("exactly at the limit fits"), Tracked.Num(), 10);
	TestEqual(TEXT("no grouped line at exactly the limit"), Summary.Count, 0);

	// One over, and there is exactly one line - not one per source that did not fit.
	Tracked.Reset();
	USoundVeilStatics::SelectTrackedSources(Few, 9, Tracked, Summary);
	TestEqual(TEXT("one source over the limit"), Tracked.Num(), 9);
	TestEqual(TEXT("one grouped source"), Summary.Count, 1);

	return true;
}

//
// (7) The tap pattern: the first tap is the engine's ray, the rest sample the opening around it.
//
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoundVeilTapPatternTest,
	"SoundVeil.Maths.TapPatternSamplesTheOpening",
	SoundVeilTests::TestFlags)

bool FSoundVeilTapPatternTest::RunTest(const FString& Parameters)
{
	const FVector Source(0.0f, 0.0f, 100.0f);
	const FVector Listener(1000.0f, 0.0f, 100.0f);
	constexpr float Radius = 60.0f;

	TArray<FVector> Starts;
	TArray<FVector> Ends;

	USoundVeilStatics::BuildTapPattern(Source, Listener, 5, Radius, Starts, Ends);

	TestEqual(TEXT("five taps in"), Starts.Num(), 5);
	TestEqual(TEXT("five taps out"), Ends.Num(), 5);

	// Tap zero is exactly the ray the engine's own occlusion would have fired, so one tap is a strict
	// downgrade to that behaviour rather than a different one.
	TestTrue(TEXT("the first tap starts at the source"), Starts[0].Equals(Source, 0.01f));
	TestTrue(TEXT("the first tap ends at the listener"), Ends[0].Equals(Listener, 0.01f));

	for (int32 Index = 1; Index < Starts.Num(); ++Index)
	{
		const float Offset = static_cast<float>(FVector::Dist(Starts[Index], Source));
		TestTrue(
			FString::Printf(TEXT("tap %d sits on the pattern radius"), Index),
			FMath::IsNearlyEqual(Offset, Radius, 0.01f));

		// The listener end moves the other way: the rays cross, which is what makes them fan through an
		// opening instead of running parallel and all hitting the same wall.
		const FVector StartOffset = Starts[Index] - Source;
		const FVector EndOffset = Ends[Index] - Listener;
		TestTrue(
			FString::Printf(TEXT("tap %d crosses the centre line"), Index),
			FVector::DotProduct(StartOffset, EndOffset) < 0.0f);
	}

	// One tap is the centre line and nothing else.
	USoundVeilStatics::BuildTapPattern(Source, Listener, 1, Radius, Starts, Ends);
	TestEqual(TEXT("one tap"), Starts.Num(), 1);
	TestTrue(TEXT("one tap is the centre line"), Starts[0].Equals(Source, 0.01f) && Ends[0].Equals(Listener, 0.01f));

	// A zero radius degrades to a bundle of centre lines rather than to a crash or an empty array.
	USoundVeilStatics::BuildTapPattern(Source, Listener, 5, 0.0f, Starts, Ends);
	TestEqual(TEXT("a zero radius still fires the asked-for taps"), Starts.Num(), 5);

	// Source and listener in the same place: still well defined, because the caller may not have
	// checked the minimum distance.
	USoundVeilStatics::BuildTapPattern(Source, Source, 5, Radius, Starts, Ends);
	TestEqual(TEXT("coincident source and listener still produce taps"), Starts.Num(), 5);

	// A silly tap count is clamped, not honoured.
	USoundVeilStatics::BuildTapPattern(Source, Listener, -4, Radius, Starts, Ends);
	TestEqual(TEXT("a negative tap count means one tap"), Starts.Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
