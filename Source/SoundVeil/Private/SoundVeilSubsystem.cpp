// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "CollisionQueryParams.h"
#include "Components/AudioComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/StringBuilder.h"
#include "SceneTypes.h"
#include "SoundVeilComponent.h"
#include "SoundVeilLog.h"
#include "SoundVeilProfile.h"
#include "SoundVeilSettings.h"
#include "SoundVeilStatics.h"
#include "Stats/Stats.h"
#include "UObject/UObjectGlobals.h"

DECLARE_STATS_GROUP(TEXT("SoundVeil"), STATGROUP_SoundVeil, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SoundVeil Update"), STAT_SoundVeilUpdate, STATGROUP_SoundVeil);
DECLARE_DWORD_COUNTER_STAT(TEXT("Occlusion Traces"), STAT_SoundVeilTraces, STATGROUP_SoundVeil);
DECLARE_DWORD_COUNTER_STAT(TEXT("Sources"), STAT_SoundVeilSources, STATGROUP_SoundVeil);

namespace SoundVeilPrivate
{
	/** Traces older than this many frames are given up on rather than blocking the source forever. */
	static constexpr int32 MaxPendingFrames = 8;

	/** Below this the applied volume/cut-off is considered unchanged and not pushed again. */
	static constexpr float VolumeEpsilon = 0.002f;
	static constexpr float FrequencyEpsilon = 5.0f;
}

// -------------------------------------------------------------------------------------------------------
// Console commands
// -------------------------------------------------------------------------------------------------------

namespace SoundVeilConsole
{
	/** Apply a lambda to the service of every world the command could plausibly mean. */
	template <typename FuncType>
	static void ForEachService(UWorld* World, FuncType Func)
	{
		TArray<USoundVeilSubsystem*> Found;

		if (World)
		{
			if (USoundVeilSubsystem* Service = World->GetSubsystem<USoundVeilSubsystem>())
			{
				Found.Add(Service);
			}
		}

		if (Found.Num() == 0 && GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					if (USoundVeilSubsystem* Service = Context.World()->GetSubsystem<USoundVeilSubsystem>())
					{
						Found.AddUnique(Service);
					}
				}
			}
		}

		for (USoundVeilSubsystem* Service : Found)
		{
			Func(*Service);
		}
	}

	static bool ParseBool(const TArray<FString>& Args, bool bDefault)
	{
		if (Args.Num() == 0)
		{
			return bDefault;
		}
		return Args[0].ToBool() || Args[0] == TEXT("1");
	}

	static FAutoConsoleCommandWithWorldAndArgs GShow(
		TEXT("SoundVeil.Show"),
		TEXT("SoundVeil.Show 0|1 - show the on-screen occlusion counter box."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bShow = ParseBool(Args, true);
			ForEachService(World, [bShow](USoundVeilSubsystem& Service) { Service.SetShowStats(bShow); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GBudget(
		TEXT("SoundVeil.Budget"),
		TEXT("SoundVeil.Budget <n> - hard cap on occlusion traces per frame, shared by every source."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogSoundVeil, Display, TEXT("SoundVeil.Budget <n>"));
				return;
			}

			const int32 Budget = FMath::Max(FCString::Atoi(*Args[0]), 0);
			ForEachService(World, [Budget](USoundVeilSubsystem& Service) { Service.SetBudget(Budget); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GTaps(
		TEXT("SoundVeil.Taps"),
		TEXT("SoundVeil.Taps <n>|Clear - force a tap count on every source. 1 is the engine's own yes/no answer."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogSoundVeil, Display, TEXT("SoundVeil.Taps <n>|Clear"));
				return;
			}

			if (Args[0].Equals(TEXT("Clear"), ESearchCase::IgnoreCase))
			{
				ForEachService(World, [](USoundVeilSubsystem& Service) { Service.ClearTapCountOverride(); });
				return;
			}

			const int32 Taps = FMath::Max(FCString::Atoi(*Args[0]), 1);
			ForEachService(World, [Taps](USoundVeilSubsystem& Service) { Service.SetTapCountOverride(Taps); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GBypass(
		TEXT("SoundVeil.Bypass"),
		TEXT("SoundVeil.Bypass 0|1 - keep measuring but stop applying. The before/after switch."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bBypass = ParseBool(Args, true);
			ForEachService(World, [bBypass](USoundVeilSubsystem& Service) { Service.SetBypass(bBypass); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GFreeze(
		TEXT("SoundVeil.Freeze"),
		TEXT("SoundVeil.Freeze 0|1 - hold every current occlusion value and stop measuring."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bFreeze = ParseBool(Args, true);
			ForEachService(World, [bFreeze](USoundVeilSubsystem& Service) { Service.Freeze(bFreeze); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs GStats(
		TEXT("SoundVeil.Stats"),
		TEXT("SoundVeil.Stats - write the current state of every source to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			ForEachService(World, [](USoundVeilSubsystem& Service) { Service.LogStats(); });
		}));
}

// -------------------------------------------------------------------------------------------------------
// Lifetime
// -------------------------------------------------------------------------------------------------------

void USoundVeilSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	Sources.Reserve(64);
	RankScratch.Reserve(64);
	RankSlots.Reserve(64);
	RoundQueue.Reserve(128);

#if WITH_EDITOR
	if (USoundVeilSettings::Get().bEnableEditorViewportStats)
	{
		EditorDrawHandle = UDebugDrawService::Register(
			TEXT("Game"),
			FDebugDrawDelegate::CreateUObject(this, &USoundVeilSubsystem::OnEditorViewportDraw));
	}
#endif
}

void USoundVeilSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (EditorDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(EditorDrawHandle);
		EditorDrawHandle.Reset();
	}
#endif

	// Hand every source its own volume back before letting go of it. A world torn down mid-occlusion
	// must not leave a pooled audio component quiet for its next user.
	for (FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive)
		{
			continue;
		}

		if (UAudioComponent* Audio = Entry.Audio.Get())
		{
			if (Entry.bApplyVolume)
			{
				Audio->SetVolumeMultiplier(Entry.BaseVolume);
			}
			if (Entry.bApplyLowpass)
			{
				Audio->SetLowPassFilterEnabled(false);
			}
		}
	}

	Sources.Reset();
	FreeSlots.Reset();
	SourceLookup.Reset();
	RoundQueue.Reset();
	RankScratch.Reset();
	RankSlots.Reset();
	TrackedScratch.Reset();
	RoundScratch.Reset();
	BuiltinProfile = nullptr;
	ResolvedDefaultProfile = nullptr;
	LiveSourceCount = 0;

	Super::Deinitialize();
}

bool USoundVeilSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only. Nothing registers in an editor world, because no component has run BeginPlay
	// there - and a service with no sources is not worth ticking.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USoundVeilSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USoundVeilSubsystem, STATGROUP_Tickables);
}

void USoundVeilSubsystem::ApplySettings()
{
	const USoundVeilSettings& Settings = USoundVeilSettings::Get();

	TraceBudgetPerFrame = FMath::Max(Settings.TraceBudgetPerFrame, 0);
	DefaultTapCount = FMath::Max(Settings.DefaultTapCount, 1);
	MaxTurnsPerSource = FMath::Clamp(Settings.MaxTurnsPerSource, 1, 8);
	MaxSources = FMath::Max(Settings.MaxSources, 1);
	StatsSourceLines = FMath::Clamp(Settings.StatsSourceLines, 0, 24);
	DefaultAttackSeconds = FMath::Max(Settings.DefaultAttackSeconds, 0.0f);
	DefaultReleaseSeconds = FMath::Max(Settings.DefaultReleaseSeconds, 0.0f);
	DefaultPatternRadius = FMath::Max(Settings.DefaultPatternRadius, 0.0f);
	bUseAsyncTraces = Settings.bUseAsyncTraces;
	bShowStats = Settings.bShowStatsByDefault;

	TapCountOverride = DefaultTapCount;
}

USoundVeilProfile* USoundVeilSubsystem::GetDefaultProfile()
{
	if (ResolvedDefaultProfile)
	{
		return ResolvedDefaultProfile;
	}

	const USoundVeilSettings& Settings = USoundVeilSettings::Get();
	if (!Settings.DefaultProfile.IsNull())
	{
		ResolvedDefaultProfile = Settings.DefaultProfile.LoadSynchronous();
		if (ResolvedDefaultProfile)
		{
			return ResolvedDefaultProfile;
		}
	}

	if (!BuiltinProfile)
	{
		// A transient fallback so a fresh project hears the effect before anybody has authored an asset.
		// Its defaults are the ones in USoundVeilProfile's constructor - a plastered wall.
		BuiltinProfile = NewObject<USoundVeilProfile>(this, TEXT("SoundVeilBuiltinProfile"));
		BuiltinProfile->TapCount = DefaultTapCount;
		BuiltinProfile->PatternRadius = DefaultPatternRadius;
		BuiltinProfile->AttackSeconds = DefaultAttackSeconds;
		BuiltinProfile->ReleaseSeconds = DefaultReleaseSeconds;
	}

	return BuiltinProfile;
}

USoundVeilProfile* USoundVeilSubsystem::EffectiveProfile(const FSourceEntry& Entry)
{
	if (USoundVeilProfile* Profile = Entry.Profile.Get())
	{
		return Profile;
	}
	return GetDefaultProfile();
}

// -------------------------------------------------------------------------------------------------------
// Registration
// -------------------------------------------------------------------------------------------------------

int32 USoundVeilSubsystem::AllocateSlot()
{
	int32 Slot = INDEX_NONE;
	if (FreeSlots.Num() > 0)
	{
		Slot = FreeSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		Slot = Sources.AddDefaulted();
	}

	FSourceEntry& Entry = Sources[Slot];
	Entry = FSourceEntry();
	Entry.Serial = NextSerial++;
	if (NextSerial == 0)
	{
		NextSerial = 1;
	}
	Entry.bAlive = true;

	++LiveSourceCount;
	bScheduleDirty = true;
	return Slot;
}

void USoundVeilSubsystem::ReleaseSlot(int32 Slot)
{
	if (!Sources.IsValidIndex(Slot) || !Sources[Slot].bAlive)
	{
		return;
	}

	FSourceEntry& Entry = Sources[Slot];
	Entry.bAlive = false;
	Entry.Pending.Reset();
	Entry.Audio.Reset();
	Entry.Component.Reset();
	Entry.Profile.Reset();
	Entry.Owner.Reset();

	FreeSlots.Add(Slot);
	--LiveSourceCount;
	bScheduleDirty = true;

	if (GroupRepresentativeSlot == Slot)
	{
		GroupRepresentativeSlot = INDEX_NONE;
	}
}

int32 USoundVeilSubsystem::ResolveSlot(const FSoundVeilSourceHandle& Handle) const
{
	if (!Handle.IsValid() || !Sources.IsValidIndex(Handle.Slot))
	{
		return INDEX_NONE;
	}

	const FSourceEntry& Entry = Sources[Handle.Slot];
	if (!Entry.bAlive || static_cast<int32>(Entry.Serial) != Handle.Serial)
	{
		return INDEX_NONE;
	}

	return Handle.Slot;
}

FSoundVeilSourceHandle USoundVeilSubsystem::RegisterSource(UAudioComponent* AudioComponent, USoundVeilProfile* Profile)
{
	FSoundVeilSourceHandle Handle;

	if (!AudioComponent)
	{
		return Handle;
	}

	// Registering the same audio component twice hands back the existing handle rather than making a
	// second row that fights the first one over the same volume multiplier.
	if (const int32* Existing = SourceLookup.Find(TObjectKey<UAudioComponent>(AudioComponent)))
	{
		if (Sources.IsValidIndex(*Existing) && Sources[*Existing].bAlive)
		{
			if (Profile)
			{
				Sources[*Existing].Profile = Profile;
			}
			Handle.Slot = *Existing;
			Handle.Serial = static_cast<int32>(Sources[*Existing].Serial);
			return Handle;
		}
	}

	const int32 Slot = AllocateSlot();
	FSourceEntry& Entry = Sources[Slot];

	Entry.Audio = AudioComponent;
	Entry.Owner = AudioComponent->GetOwner();
	Entry.Profile = Profile;
	Entry.BaseVolume = AudioComponent->VolumeMultiplier;
	Entry.Location = AudioComponent->GetComponentLocation();
	Entry.Priority = Profile ? Profile->Priority : 1.0f;

	SourceLookup.Add(TObjectKey<UAudioComponent>(AudioComponent), Slot);

	Handle.Slot = Slot;
	Handle.Serial = static_cast<int32>(Entry.Serial);
	return Handle;
}

FSoundVeilSourceHandle USoundVeilSubsystem::RegisterComponentSource(USoundVeilComponent* Source, UAudioComponent* AudioComponent)
{
	FSoundVeilSourceHandle Handle = RegisterSource(AudioComponent, Source ? Source->Profile : nullptr);

	const int32 Slot = ResolveSlot(Handle);
	if (Slot != INDEX_NONE && Source)
	{
		FSourceEntry& Entry = Sources[Slot];
		Entry.Component = Source;
		Entry.bApplyVolume = Source->bApplyVolume;
		Entry.bApplyLowpass = Source->bApplyLowpass;
		Entry.Owner = Source->GetOwner();
	}

	return Handle;
}

void USoundVeilSubsystem::UnregisterSource(FSoundVeilSourceHandle Handle)
{
	const int32 Slot = ResolveSlot(Handle);
	if (Slot == INDEX_NONE)
	{
		return;
	}

	FSourceEntry& Entry = Sources[Slot];

	if (UAudioComponent* Audio = Entry.Audio.Get())
	{
		if (Entry.bApplyVolume)
		{
			Audio->SetVolumeMultiplier(Entry.BaseVolume);
		}
		if (Entry.bApplyLowpass)
		{
			Audio->SetLowPassFilterEnabled(false);
		}
		SourceLookup.Remove(TObjectKey<UAudioComponent>(Audio));
	}
	else
	{
		// The audio component is already gone, so the lookup key cannot be rebuilt from it. Sweep.
		for (auto It = SourceLookup.CreateIterator(); It; ++It)
		{
			if (It.Value() == Slot)
			{
				It.RemoveCurrent();
			}
		}
	}

	ReleaseSlot(Slot);
}

bool USoundVeilSubsystem::IsSourceRegistered(FSoundVeilSourceHandle Handle) const
{
	return ResolveSlot(Handle) != INDEX_NONE;
}

// -------------------------------------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------------------------------------

float USoundVeilSubsystem::GetOcclusion(FSoundVeilSourceHandle Handle) const
{
	const int32 Slot = ResolveSlot(Handle);
	return Slot == INDEX_NONE ? 0.0f : Sources[Slot].Occlusion;
}

bool USoundVeilSubsystem::GetSourceState(FSoundVeilSourceHandle Handle, FSoundVeilSourceState& OutState) const
{
	const int32 Slot = ResolveSlot(Handle);
	if (Slot == INDEX_NONE)
	{
		OutState = FSoundVeilSourceState();
		return false;
	}

	const FSourceEntry& Entry = Sources[Slot];

	OutState.Handle = Handle;
	OutState.Owner = Entry.Owner;
	OutState.Occlusion = Entry.Occlusion;
	OutState.TargetOcclusion = Entry.TargetOcclusion;
	OutState.VolumeMultiplier = Entry.VolumeMultiplier;
	OutState.LowpassFrequency = Entry.LowpassFrequency;
	OutState.Distance = Entry.Distance;
	OutState.Loudness = Entry.BaseVolume;
	OutState.FramesSinceUpdate = Entry.FramesSinceUpdate;
	OutState.LastTapCount = Entry.LastTapCount;
	OutState.bAudible = Entry.bAudible;
	OutState.bGrouped = Entry.bGrouped;
	return true;
}

void USoundVeilSubsystem::GetLoudestSources(int32 Count, TArray<FSoundVeilSourceState>& OutStates) const
{
	OutStates.Reset();

	if (Count <= 0)
	{
		return;
	}

	TArray<int32> Order;
	Order.Reserve(LiveSourceCount);
	for (int32 Slot = 0; Slot < Sources.Num(); ++Slot)
	{
		if (Sources[Slot].bAlive && Sources[Slot].bAudible)
		{
			Order.Add(Slot);
		}
	}

	Order.Sort([this](int32 A, int32 B)
	{
		FSoundVeilSourceRank RankA;
		RankA.Loudness = Sources[A].BaseVolume;
		RankA.Distance = Sources[A].Distance;
		RankA.Priority = Sources[A].Priority;

		FSoundVeilSourceRank RankB;
		RankB.Loudness = Sources[B].BaseVolume;
		RankB.Distance = Sources[B].Distance;
		RankB.Priority = Sources[B].Priority;

		const float ScoreA = USoundVeilStatics::ScoreSource(RankA);
		const float ScoreB = USoundVeilStatics::ScoreSource(RankB);
		if (ScoreA != ScoreB)
		{
			return ScoreA > ScoreB;
		}
		return A < B;
	});

	const int32 Num = FMath::Min(Count, Order.Num());
	OutStates.Reserve(Num);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		const int32 Slot = Order[Index];
		FSoundVeilSourceHandle Handle;
		Handle.Slot = Slot;
		Handle.Serial = static_cast<int32>(Sources[Slot].Serial);

		FSoundVeilSourceState State;
		if (GetSourceState(Handle, State))
		{
			OutStates.Add(State);
		}
	}
}

// -------------------------------------------------------------------------------------------------------
// Budget and switches
// -------------------------------------------------------------------------------------------------------

void USoundVeilSubsystem::SetBudget(int32 InTracesPerFrame)
{
	TraceBudgetPerFrame = FMath::Max(InTracesPerFrame, 0);
}

void USoundVeilSubsystem::SetTapCountOverride(int32 InTapCount)
{
	TapCountOverride = FMath::Max(InTapCount, 1);
	bHasTapCountOverride = true;
}

void USoundVeilSubsystem::ClearTapCountOverride()
{
	bHasTapCountOverride = false;
}

int32 USoundVeilSubsystem::GetEffectiveTapCount(const USoundVeilProfile* Profile) const
{
	if (bHasTapCountOverride)
	{
		return TapCountOverride;
	}
	return Profile ? FMath::Max(Profile->TapCount, 1) : DefaultTapCount;
}

void USoundVeilSubsystem::SetBypass(bool bInBypass)
{
	if (bBypass == bInBypass)
	{
		return;
	}

	bBypass = bInBypass;

	// Push the change out now rather than on the next smoothing pass: the switch is what a listener is
	// A/B-ing, and a frame of lag on it is a frame of doubt about what they just heard.
	for (FSourceEntry& Entry : Sources)
	{
		if (Entry.bAlive)
		{
			ApplyToAudio(Entry);
		}
	}
}

void USoundVeilSubsystem::Freeze(bool bInFrozen)
{
	bFrozen = bInFrozen;
}

void USoundVeilSubsystem::SetListenerActor(AActor* InListener)
{
	ListenerOverride = InListener;
}

// -------------------------------------------------------------------------------------------------------
// Tick
// -------------------------------------------------------------------------------------------------------

void USoundVeilSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SCOPE_CYCLE_COUNTER(STAT_SoundVeilUpdate);
	const double StartTime = FPlatformTime::Seconds();

	Stats.TracesThisFrame = 0;
	Stats.EvaluationsThisFrame = 0;

	UpdateListener();

	// Order matters: the wait counter goes up for everybody first, and only then do this frame's
	// finished traces reset it. The other way round, a source that was measured this very frame would
	// still report a wait of one, and the counter box would show a longest wait above its own limit.
	RefreshSources();
	CollectTraceResults();

	if (!bFrozen)
	{
		if (bScheduleDirty || RoundCursor >= RoundQueue.Num())
		{
			RebuildSchedule();
		}

		RunMeasurementRound();
	}

	SmoothAndApply(DeltaTime);

	// ------------------------------------------------------------------------------------------------
	// Statistics
	// ------------------------------------------------------------------------------------------------

	int32 Audible = 0;
	int32 Grouped = 0;
	int32 LongestWait = 0;
	double OcclusionSum = 0.0;

	for (const FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive)
		{
			continue;
		}

		if (Entry.bGrouped)
		{
			++Grouped;
		}

		if (Entry.bAudible)
		{
			++Audible;
			OcclusionSum += Entry.Occlusion;
			LongestWait = FMath::Max(LongestWait, Entry.FramesSinceUpdate);
		}
	}

	Stats.Sources = LiveSourceCount;
	Stats.AudibleSources = Audible;
	Stats.GroupedSources = Grouped;
	Stats.TraceBudget = TraceBudgetPerFrame;

	// What the same scene would have cost with the engine's one-ray-per-source occlusion. The point of
	// the box is this comparison: the left number is fixed, the right one grows with the level.
	Stats.NaiveTraces = Audible;

	Stats.LongestWaitFrames = LongestWait;
	Stats.RoundLength = RoundQueue.Num();

	// The scheduling bound plus the one frame an async trace spends in flight. Counting that frame is
	// the difference between a limit the measured wait respects and a limit it quietly exceeds.
	Stats.WaitLimitFrames = USoundVeilStatics::WaitLimitFrames(
		RoundQueue.Num(), TraceBudgetPerFrame, GetEffectiveTapCount(nullptr))
		+ (bUseAsyncTraces ? 1 : 0);
	Stats.MeanOcclusion = Audible > 0 ? static_cast<float>(OcclusionSum / static_cast<double>(Audible)) : 0.0f;
	Stats.bBypassed = bBypass;
	Stats.bFrozen = bFrozen;
	Stats.UpdateMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	SET_DWORD_STAT(STAT_SoundVeilTraces, Stats.TracesThisFrame);
	SET_DWORD_STAT(STAT_SoundVeilSources, Stats.Sources);
}

void USoundVeilSubsystem::UpdateListener()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (AActor* Override = ListenerOverride.Get())
	{
		ListenerLocation = Override->GetActorLocation();
		ListenerActor = Override;
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController || !PlayerController->IsLocalController())
		{
			continue;
		}

		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

		ListenerLocation = ViewLocation;

		// The view target, not the controller: the pawn is the thing standing in the world with a
		// capsule that would block every single tap if it were not ignored.
		ListenerActor = PlayerController->GetViewTarget();
		return;
	}
}

void USoundVeilSubsystem::RefreshSources()
{
	for (FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive)
		{
			continue;
		}

		UAudioComponent* Audio = Entry.Audio.Get();
		if (!Audio)
		{
			// The audio component died without anybody unregistering it. Retire the row quietly.
			ReleaseSlot(static_cast<int32>(&Entry - Sources.GetData()));
			continue;
		}

		Entry.Location = Audio->GetComponentLocation();
		Entry.Distance = static_cast<float>(FVector::Dist(Entry.Location, ListenerLocation));
		++Entry.FramesSinceUpdate;

		const USoundVeilProfile* Profile = Entry.Profile.Get();
		const float MaxDistance = Profile ? Profile->MaxAudibleDistance : 8000.0f;
		Entry.Priority = Profile ? Profile->Priority : 1.0f;

		// Audible means "worth spending a trace on", not "the player can hear it". A stopped component,
		// a muted one, or one past its own attenuation radius keeps whatever value it had and costs
		// nothing at all - which is where most of the saving in a big level actually comes from.
		Entry.bAudible = Audio->IsPlaying()
			&& Entry.BaseVolume > KINDA_SMALL_NUMBER
			&& Entry.Distance <= MaxDistance;
	}
}

void USoundVeilSubsystem::CollectTraceResults()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive || Entry.Pending.Num() == 0)
		{
			continue;
		}

		++Entry.PendingFrames;

		for (int32 Index = Entry.Pending.Num() - 1; Index >= 0; --Index)
		{
			FTraceDatum Datum;
			if (!World->QueryTraceData(Entry.Pending[Index], Datum))
			{
				continue;
			}

			bool bBlocked = false;
			for (const FHitResult& Hit : Datum.OutHits)
			{
				if (Hit.bBlockingHit)
				{
					bBlocked = true;
					break;
				}
			}

			Entry.PendingBlocked += bBlocked ? 1 : 0;
			++Entry.PendingResolved;
			Entry.Pending.RemoveAtSwap(Index, EAllowShrinking::No);
		}

		const bool bTimedOut = Entry.PendingFrames > SoundVeilPrivate::MaxPendingFrames;

		if (Entry.Pending.Num() == 0 || bTimedOut)
		{
			if (Entry.PendingResolved > 0)
			{
				Entry.TargetOcclusion = USoundVeilStatics::EvaluateOcclusion(Entry.PendingBlocked, Entry.PendingResolved);
				Entry.LastTapCount = Entry.PendingResolved;
				Entry.FramesSinceUpdate = 0;

				if (Entry.bGrouped)
				{
					GroupTargetOcclusion = Entry.TargetOcclusion;
				}
			}

			// A set that never came back is dropped rather than retried forever: the source keeps its
			// last value, which is exactly what a source that missed its turn does anyway.
			Entry.Pending.Reset();
			Entry.PendingTaps = 0;
			Entry.PendingBlocked = 0;
			Entry.PendingResolved = 0;
			Entry.PendingFrames = 0;
		}
	}
}

void USoundVeilSubsystem::RebuildSchedule()
{
	RankScratch.Reset();
	RankSlots.Reset();

	for (int32 Slot = 0; Slot < Sources.Num(); ++Slot)
	{
		const FSourceEntry& Entry = Sources[Slot];
		if (!Entry.bAlive)
		{
			continue;
		}

		FSoundVeilSourceRank Rank;
		Rank.Loudness = Entry.BaseVolume;
		Rank.Distance = Entry.Distance;
		Rank.Priority = Entry.Priority;
		Rank.FramesSinceUpdate = Entry.FramesSinceUpdate;
		Rank.bAudible = Entry.bAudible;

		RankScratch.Add(Rank);
		RankSlots.Add(Slot);
	}

	USoundVeilStatics::SelectTrackedSources(RankScratch, MaxSources, TrackedScratch, GroupSummary);

	// Everybody is grouped until the tracked set says otherwise. One pass, no second array.
	for (int32 Slot : RankSlots)
	{
		Sources[Slot].bGrouped = true;
	}

	TArray<FSoundVeilSourceRank> ScheduleRanks;
	TArray<int32> ScheduleSlots;
	ScheduleRanks.Reserve(TrackedScratch.Num() + 1);
	ScheduleSlots.Reserve(TrackedScratch.Num() + 1);

	for (int32 Index : TrackedScratch)
	{
		const int32 Slot = RankSlots[Index];
		Sources[Slot].bGrouped = false;
		ScheduleRanks.Add(RankScratch[Index]);
		ScheduleSlots.Add(Slot);
	}

	// The grouped set gets exactly one turn in the round, taken by its best-scoring member. That one
	// measurement is what the whole group runs on - the alternative is either measuring hundreds of
	// distant loops individually or letting them run unoccluded, and both are worse than one shared,
	// honestly measured value.
	GroupRepresentativeSlot = INDEX_NONE;
	if (GroupSummary.Count > 0)
	{
		float BestScore = -1.0f;
		int32 BestIndex = INDEX_NONE;

		for (int32 Index = 0; Index < RankScratch.Num(); ++Index)
		{
			const int32 Slot = RankSlots[Index];
			if (!Sources[Slot].bGrouped || !RankScratch[Index].bAudible)
			{
				continue;
			}

			const float Score = USoundVeilStatics::ScoreSource(RankScratch[Index]);
			if (Score > BestScore)
			{
				BestScore = Score;
				BestIndex = Index;
			}
		}

		if (BestIndex != INDEX_NONE)
		{
			GroupRepresentativeSlot = RankSlots[BestIndex];
			ScheduleRanks.Add(RankScratch[BestIndex]);
			ScheduleSlots.Add(GroupRepresentativeSlot);
		}
	}

	RoundScratch.Reset();
	USoundVeilStatics::RankSources(ScheduleRanks, MaxTurnsPerSource, RoundScratch);

	RoundQueue.Reset();
	RoundQueue.Reserve(RoundScratch.Num());
	for (int32 Index : RoundScratch)
	{
		if (ScheduleSlots.IsValidIndex(Index))
		{
			RoundQueue.Add(ScheduleSlots[Index]);
		}
	}

	RoundCursor = 0;
	bScheduleDirty = false;
}

void USoundVeilSubsystem::RunMeasurementRound()
{
	int32 Remaining = TraceBudgetPerFrame;
	if (Remaining <= 0 || RoundQueue.Num() == 0)
	{
		return;
	}

	while (Remaining > 0 && RoundCursor < RoundQueue.Num())
	{
		const int32 Slot = RoundQueue[RoundCursor++];
		if (!Sources.IsValidIndex(Slot))
		{
			continue;
		}

		FSourceEntry& Entry = Sources[Slot];
		if (!Entry.bAlive || !Entry.bAudible || Entry.Pending.Num() > 0)
		{
			// A turn spent on a source that is silent, gone, or still waiting for last frame's traces
			// costs nothing and is simply skipped - the budget goes to the next one in the round.
			continue;
		}

		const int32 Spent = MeasureSource(Slot, Remaining);
		if (Spent <= 0)
		{
			continue;
		}

		Remaining -= Spent;
		Stats.TracesThisFrame += Spent;
		++Stats.EvaluationsThisFrame;
	}
}

int32 USoundVeilSubsystem::MeasureSource(int32 Slot, int32 MaxTaps)
{
	UWorld* World = GetWorld();
	if (!World || !Sources.IsValidIndex(Slot))
	{
		return 0;
	}

	FSourceEntry& Entry = Sources[Slot];
	const USoundVeilProfile* Profile = EffectiveProfile(Entry);

	const float MinDistance = Profile ? Profile->MinDistance : 120.0f;
	if (Entry.Distance <= MinDistance)
	{
		// Standing on top of the source. Two points that close have geometry between them only because
		// they are almost the same point; the honest answer is "open".
		Entry.TargetOcclusion = 0.0f;
		Entry.LastTapCount = 0;
		Entry.FramesSinceUpdate = 0;
		return 0;
	}

	// The measurement takes what is left of the frame's ration rather than being skipped for want of a
	// full pattern. At one tap it degrades to exactly the engine's yes/no answer, which is a worse
	// measurement but never a missing one.
	const int32 WantedTaps = FMath::Max(GetEffectiveTapCount(Profile), 1);
	const int32 Taps = FMath::Clamp(WantedTaps, 1, FMath::Max(MaxTaps, 1));

	const float PatternRadius = Profile ? Profile->PatternRadius : DefaultPatternRadius;
	USoundVeilStatics::BuildTapPattern(Entry.Location, ListenerLocation, Taps, PatternRadius, TapStarts, TapEnds);

	FCollisionQueryParams Params;
	BuildQueryParams(Entry, Params);

	const ECollisionChannel Channel = Profile ? Profile->TraceChannel.GetValue() : ECC_Visibility;

	if (!bUseAsyncTraces)
	{
		int32 Blocked = 0;
		for (int32 Index = 0; Index < Taps; ++Index)
		{
			if (World->LineTraceTestByChannel(TapStarts[Index], TapEnds[Index], Channel, Params))
			{
				++Blocked;
			}
		}

		Entry.TargetOcclusion = USoundVeilStatics::EvaluateOcclusion(Blocked, Taps);
		Entry.LastTapCount = Taps;
		Entry.FramesSinceUpdate = 0;

		if (Entry.bGrouped)
		{
			GroupTargetOcclusion = Entry.TargetOcclusion;
		}

		return Taps;
	}

	Entry.Pending.Reset();
	Entry.Pending.Reserve(Taps);
	Entry.PendingTaps = Taps;
	Entry.PendingBlocked = 0;
	Entry.PendingResolved = 0;
	Entry.PendingFrames = 0;

	for (int32 Index = 0; Index < Taps; ++Index)
	{
		const FTraceHandle Handle = World->AsyncLineTraceByChannel(
			EAsyncTraceType::Single, TapStarts[Index], TapEnds[Index], Channel, Params);

		if (Handle.IsValid())
		{
			Entry.Pending.Add(Handle);
		}
	}

	if (Entry.Pending.Num() == 0)
	{
		Entry.PendingTaps = 0;
		return 0;
	}

	return Entry.Pending.Num();
}

void USoundVeilSubsystem::BuildQueryParams(const FSourceEntry& Entry, FCollisionQueryParams& OutParams) const
{
	OutParams = FCollisionQueryParams(SCENE_QUERY_STAT(SoundVeilOcclusion), /*bTraceComplex=*/false);
	OutParams.bReturnPhysicalMaterial = false;

	if (const AActor* Owner = Entry.Owner.Get())
	{
		OutParams.AddIgnoredActor(Owner);
	}

	if (const AActor* Listener = ListenerActor.Get())
	{
		OutParams.AddIgnoredActor(Listener);
	}
}

void USoundVeilSubsystem::SmoothAndApply(float DeltaTime)
{
	// The group value is smoothed once, here, rather than once per grouped source: they share it.
	const USoundVeilProfile* GroupProfile = GroupRepresentativeSlot != INDEX_NONE && Sources.IsValidIndex(GroupRepresentativeSlot)
		? Sources[GroupRepresentativeSlot].Profile.Get()
		: nullptr;

	GroupOcclusion = USoundVeilStatics::SmoothOcclusion(
		GroupOcclusion,
		GroupTargetOcclusion,
		bFrozen ? 0.0f : DeltaTime,
		GroupProfile ? GroupProfile->AttackSeconds : DefaultAttackSeconds,
		GroupProfile ? GroupProfile->ReleaseSeconds : DefaultReleaseSeconds);
	GroupSummary.Occlusion = GroupOcclusion;

	for (FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive)
		{
			continue;
		}

		const USoundVeilProfile* Profile = Entry.Profile.Get();
		const float Attack = Profile ? Profile->AttackSeconds : DefaultAttackSeconds;
		const float Release = Profile ? Profile->ReleaseSeconds : DefaultReleaseSeconds;

		// A grouped source follows the group's value, except for the representative, which measured it.
		const int32 Slot = static_cast<int32>(&Entry - Sources.GetData());
		const float Target = (Entry.bGrouped && Slot != GroupRepresentativeSlot)
			? GroupOcclusion
			: Entry.TargetOcclusion;

		Entry.Occlusion = USoundVeilStatics::SmoothOcclusion(
			Entry.Occlusion, Target, bFrozen ? 0.0f : DeltaTime, Attack, Release);

		ApplyToAudio(Entry);
	}
}

void USoundVeilSubsystem::ApplyToAudio(FSourceEntry& Entry)
{
	UAudioComponent* Audio = Entry.Audio.Get();
	if (!Audio)
	{
		return;
	}

	// The curves are evaluated even while bypassed, and the result is what the counter box reports.
	// That is the whole worth of the bypass switch: the numbers on screen are what the plugin would be
	// doing to this source right now, next to a source it is audibly not doing it to.
	float Volume = 1.0f;
	float Lowpass = USoundVeilProfile::MaxLowpassFrequency;
	USoundVeilStatics::ApplyCurves(Entry.Occlusion, Entry.Profile.Get(), Volume, Lowpass);

	Entry.VolumeMultiplier = Volume;
	Entry.LowpassFrequency = Lowpass;

	const float AppliedVolume = bBypass ? 1.0f : Volume;
	const float AppliedLowpass = bBypass ? USoundVeilProfile::MaxLowpassFrequency : Lowpass;

	// Only pushed when it actually moved. A hundred audio components told the same number every frame
	// is a hundred audio commands a frame, and the whole plugin is about not doing that sort of thing.
	if (Entry.bApplyVolume && !FMath::IsNearlyEqual(Entry.AppliedVolume, AppliedVolume, SoundVeilPrivate::VolumeEpsilon))
	{
		Audio->SetVolumeMultiplier(Entry.BaseVolume * AppliedVolume);
		Entry.AppliedVolume = AppliedVolume;
	}

	if (Entry.bApplyLowpass && !FMath::IsNearlyEqual(Entry.AppliedLowpass, AppliedLowpass, SoundVeilPrivate::FrequencyEpsilon))
	{
		const bool bFilterOn = AppliedLowpass < USoundVeilProfile::MaxLowpassFrequency - 1.0f;
		Audio->SetLowPassFilterEnabled(bFilterOn);
		if (bFilterOn)
		{
			Audio->SetLowPassFilterFrequency(AppliedLowpass);
		}
		Entry.AppliedLowpass = AppliedLowpass;
	}

	if (USoundVeilComponent* Component = Entry.Component.Get())
	{
		Component->NotifyOcclusion(Entry.Occlusion, Volume, Lowpass);
	}
}

// -------------------------------------------------------------------------------------------------------
// Display
// -------------------------------------------------------------------------------------------------------

void USoundVeilSubsystem::DrawStats(UCanvas* Canvas)
{
	if (!Canvas || !bShowStats)
	{
		return;
	}

	LastHudDrawFrame = GFrameCounter;

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	TArray<FSoundVeilSourceState> Loudest;
	GetLoudestSources(StatsSourceLines, Loudest);

	constexpr float BoxX = 24.0f;
	constexpr float BoxY = 90.0f;
	constexpr float BoxWidth = 352.0f;
	constexpr float LineHeight = 15.0f;

	const int32 HeaderLines = 9;
	const int32 GroupLines = GroupSummary.Count > 0 ? 1 : 0;
	const int32 LineCount = HeaderLines + (Loudest.Num() > 0 ? Loudest.Num() + 1 : 0) + GroupLines;

	FCanvasTileItem Background(
		FVector2D(BoxX - 8.0f, BoxY - 8.0f),
		GWhiteTexture,
		FVector2D(BoxWidth, LineCount * LineHeight + 16.0f),
		FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Background);

	float LineY = BoxY;

	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(BoxX, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	const FLinearColor Heading(0.55f, 0.85f, 1.0f, 1.0f);
	const FLinearColor Body(0.9f, 0.9f, 0.9f, 1.0f);
	const FLinearColor Good(0.55f, 0.95f, 0.55f, 1.0f);
	const FLinearColor Warn(1.0f, 0.75f, 0.35f, 1.0f);

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Append(TEXT("SoundVeil"));
	if (bFrozen)
	{
		Line.Append(TEXT("   [frozen]"));
	}
	if (bBypass)
	{
		Line.Append(TEXT("   [bypassed]"));
	}
	DrawLine(Line.ToView(), Heading);

	Line.Reset();
	Line.Appendf(TEXT("Traces           %d / %d   (naive %d)"), Stats.TracesThisFrame, Stats.TraceBudget, Stats.NaiveTraces);
	DrawLine(Line.ToView(), Stats.TracesThisFrame <= Stats.TraceBudget ? Good : Warn);

	Line.Reset();
	Line.Appendf(TEXT("Sources          %d   (%d audible)"), Stats.Sources, Stats.AudibleSources);
	DrawLine(Line.ToView(), Body);

	Line.Reset();
	Line.Appendf(TEXT("Measured         %d this frame"), Stats.EvaluationsThisFrame);
	DrawLine(Line.ToView(), Body);

	Line.Reset();
	Line.Appendf(TEXT("Longest wait     %d frames   (limit %d)"), Stats.LongestWaitFrames, Stats.WaitLimitFrames);
	DrawLine(Line.ToView(), Stats.LongestWaitFrames <= Stats.WaitLimitFrames ? Good : Warn);

	Line.Reset();
	Line.Appendf(TEXT("Round length     %d"), Stats.RoundLength);
	DrawLine(Line.ToView(), Body);

	Line.Reset();
	Line.Appendf(TEXT("Taps             %d%s"), GetEffectiveTapCount(nullptr), bHasTapCountOverride ? TEXT(" (forced)") : TEXT(" (profile)"));
	DrawLine(Line.ToView(), Body);

	Line.Reset();
	Line.Appendf(TEXT("Mean occlusion   %.2f"), Stats.MeanOcclusion);
	DrawLine(Line.ToView(), Body);

	Line.Reset();
	Line.Appendf(TEXT("Update           %.3f ms"), Stats.UpdateMs);
	DrawLine(Line.ToView(), Body);

	if (Loudest.Num() > 0)
	{
		Line.Reset();
		Line.Append(TEXT("Source            occl   vol    lowpass"));
		DrawLine(Line.ToView(), Heading);

		for (const FSoundVeilSourceState& State : Loudest)
		{
			const AActor* Owner = State.Owner.Get();
			FString Name = Owner ? Owner->GetName() : TEXT("<source>");
			if (Name.Len() > 16)
			{
				Name = Name.Right(16);
			}

			Line.Reset();
			Line.Appendf(TEXT("%-16s  %4.2f   %4.2f   %5.0f Hz"),
				*Name, State.Occlusion, State.VolumeMultiplier, State.LowpassFrequency);

			// Dark for covered, bright for open - the same reading the demo columns use, so the box and
			// the level agree at a glance in a still image.
			const float Brightness = FMath::Lerp(1.0f, 0.45f, FMath::Clamp(State.Occlusion, 0.0f, 1.0f));
			DrawLine(Line.ToView(), FLinearColor(Brightness, Brightness, Brightness, 1.0f));
		}
	}

	if (GroupSummary.Count > 0)
	{
		Line.Reset();
		Line.Appendf(TEXT("+%d grouped        %4.2f   (past Max Sources)"), GroupSummary.Count, GroupSummary.Occlusion);
		DrawLine(Line.ToView(), Warn);
	}
}

void USoundVeilSubsystem::LogStats() const
{
	UE_LOG(LogSoundVeil, Display,
		TEXT("SoundVeil: %d sources (%d audible, %d grouped), %d/%d traces, naive %d, longest wait %d/%d frames, round %d, mean occlusion %.2f%s%s"),
		Stats.Sources, Stats.AudibleSources, Stats.GroupedSources,
		Stats.TracesThisFrame, Stats.TraceBudget, Stats.NaiveTraces,
		Stats.LongestWaitFrames, Stats.WaitLimitFrames, Stats.RoundLength, Stats.MeanOcclusion,
		bBypass ? TEXT(" [bypassed]") : TEXT(""), bFrozen ? TEXT(" [frozen]") : TEXT(""));

	for (const FSourceEntry& Entry : Sources)
	{
		if (!Entry.bAlive)
		{
			continue;
		}

		const AActor* Owner = Entry.Owner.Get();
		UE_LOG(LogSoundVeil, Display,
			TEXT("  %-24s occl %.2f (target %.2f) vol %.2f lowpass %.0f Hz  %.0f cm  taps %d  waited %d%s%s"),
			Owner ? *Owner->GetName() : TEXT("<source>"),
			Entry.Occlusion, Entry.TargetOcclusion, Entry.VolumeMultiplier, Entry.LowpassFrequency,
			Entry.Distance, Entry.LastTapCount, Entry.FramesSinceUpdate,
			Entry.bGrouped ? TEXT(" [grouped]") : TEXT(""),
			Entry.bAudible ? TEXT("") : TEXT(" [inaudible]"));
	}
}

#if WITH_EDITOR
void USoundVeilSubsystem::OnEditorViewportDraw(UCanvas* Canvas, APlayerController* PlayerController)
{
	if (!Canvas || !bShowStats)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || Canvas->SceneView == nullptr)
	{
		return;
	}

	// A HUD already drew this frame, so this pass would put a second box on top of the first.
	if (LastHudDrawFrame == GFrameCounter)
	{
		return;
	}

	const uint64 SavedFrame = LastHudDrawFrame;
	DrawStats(Canvas);
	LastHudDrawFrame = SavedFrame;
}
#endif
