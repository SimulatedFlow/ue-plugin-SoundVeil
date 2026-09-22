// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SoundVeilTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "WorldCollision.h"
#include "SoundVeilSubsystem.generated.h"

class AActor;
class APlayerController;
class UAudioComponent;
class UCanvas;
class USceneComponent;
class USoundVeilComponent;
class USoundVeilProfile;

/**
 * The service. One per world, ticks itself, owns every registered sound source in that world.
 *
 * The naive way to occlude a sound is one line trace per source per frame, straight from the engine's
 * attenuation settings. Sixty ambient sources on a floor is sixty traces every frame - and forty of
 * them are for sources on the other side of the building, which the player could not hear either way.
 * The answer they produce is a single bit, so a half open door is a wall until the ray happens to slip
 * through the gap.
 *
 * SoundVeil replaces that with four things:
 *
 *   1. One shared budget. Eight traces per frame across every source in the world, by default. A
 *      source that does not get a turn keeps the value it already had, so nothing stalls and nothing
 *      pops - it is simply measured less often. Sixty sources at one trace each is sixty traces a
 *      frame; here it is eight, no matter how many sources there are, and no source waits longer than
 *      the round length divided by the measurements that fit in a frame. That number is on screen.
 *      (Same rule as TurretMind's acquisition round - budgeted round robin over a shared service is
 *      the shape this problem keeps having.)
 *
 *   2. Several taps instead of one ray. A measurement is TapCount rays across a small pattern around
 *      the line, and the occlusion is how many of them were blocked. Four of five is 0.8, so the door
 *      swinging open reads as 1.0, 0.8, 0.6, 0.2, 0.0 instead of 1, 1, 0, 0, 0.
 *
 *   3. Attack and release per source. The measurement is a target, not a value; each source walks
 *      towards it over its profile's attack or release time. Somebody crossing the line of sight for
 *      two frames never reaches the mix as a click.
 *
 *   4. Two knobs out, not one switch. Occlusion drives a volume multiplier and a low pass cut-off
 *      through two curves in the profile, so concrete and a curtain can damp differently without
 *      touching code.
 *
 * The traces are asynchronous and read back on the following frame. A synchronous trace fired from the
 * audio update blocks the game thread on the physics scene, which is exactly the cost this plugin
 * exists to remove - firing sixty of them cheaply would be no improvement at all.
 *
 * What this is not: it is not a spatializer, not an audio engine and not a reverb solver. It computes
 * no sound paths and no diffraction. It sets two values per source that the existing audio chain
 * already understands, and stops there. It runs happily next to PulseScore - the two touch different
 * things: PulseScore mixes the stems of one piece of music, SoundVeil damps individual sources in the
 * room.
 */
UCLASS()
class SOUNDVEIL_API USoundVeilSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// --------------------------------------------------------------------------------------------------
	// Registration
	// --------------------------------------------------------------------------------------------------

	/**
	 * Put an audio component under the service's care. Returns the handle to ask about it later.
	 *
	 * This is the way in for anything that is not using USoundVeilComponent - a pooled one-shot, a
	 * spawned emitter, an audio component a system of yours already owns. The component's volume
	 * multiplier at the moment of registration is remembered as the source's own level; everything
	 * SoundVeil does afterwards is a factor on top of it, so registering twice or unregistering never
	 * leaves a source quieter than it started.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	FSoundVeilSourceHandle RegisterSource(UAudioComponent* AudioComponent, USoundVeilProfile* Profile);

	/** Take a source out again and hand its own volume back. Safe to call twice, safe during shutdown. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void UnregisterSource(FSoundVeilSourceHandle Handle);

	/** Registration path used by USoundVeilComponent, which also wants the occlusion reported back. */
	FSoundVeilSourceHandle RegisterComponentSource(USoundVeilComponent* Source, UAudioComponent* AudioComponent);

	/** True while the handle names a live source. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	bool IsSourceRegistered(FSoundVeilSourceHandle Handle) const;

	// --------------------------------------------------------------------------------------------------
	// Queries
	// --------------------------------------------------------------------------------------------------

	/** Smoothed occlusion of one source, 0 = clear line, 1 = fully covered. Unknown handles give 0. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	float GetOcclusion(FSoundVeilSourceHandle Handle) const;

	/** Everything the counter box knows about one source. False when the handle is stale. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	bool GetSourceState(FSoundVeilSourceHandle Handle, FSoundVeilSourceState& OutState) const;

	/** The loudest sources first, at most Count of them. What the counter box lists. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void GetLoudestSources(int32 Count, TArray<FSoundVeilSourceState>& OutStates) const;

	/** What the service did on the most recent frame. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	const FSoundVeilStats& GetStats() const { return Stats; }

	/** The one shared occlusion value the grouped sources past Max Sources are running on. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	float GetGroupedOcclusion() const { return GroupOcclusion; }

	// --------------------------------------------------------------------------------------------------
	// Budget and switches
	// --------------------------------------------------------------------------------------------------

	/** Move the hard cap on traces started per frame, shared by every source. Takes effect next frame. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void SetBudget(int32 InTracesPerFrame);

	/** Current per-frame trace budget. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	int32 GetBudget() const { return TraceBudgetPerFrame; }

	/** Force one tap count on every source, whatever their profiles say. 1 is the engine's behaviour. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void SetTapCountOverride(int32 InTapCount);

	/** Hand the tap count back to each profile. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void ClearTapCountOverride();

	/** True while a tap count override is in force. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	bool HasTapCountOverride() const { return bHasTapCountOverride; }

	/** The forced tap count, or the project default when no override is in force. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	int32 GetEffectiveTapCount(const USoundVeilProfile* Profile) const;

	/**
	 * The before/after switch. Bypassed, every source runs at its own volume with the filter off, while
	 * the service keeps measuring - so the counter box shows what it would have done, and flipping the
	 * switch back does not have to re-measure anything.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void SetBypass(bool bInBypass);

	/** True while occlusion is measured but not applied. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	bool IsBypassed() const { return bBypass; }

	/**
	 * Stop measuring and hold every current value.
	 *
	 * For a pause menu, and for a screenshot: a frozen service still draws its counter box and still
	 * applies what it last measured, so the numbers on two screenshots taken seconds apart are
	 * comparable.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void Freeze(bool bInFrozen);

	/** True while measurement is held. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	bool IsFrozen() const { return bFrozen; }

	/**
	 * Measure occlusion from this actor's location instead of the local player's camera.
	 *
	 * Split screen, a spectator camera, or a game where the ears are not where the eyes are. Null hands
	 * it back to the local player controller's view point.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void SetListenerActor(AActor* InListener);

	/** Where the service is measuring from this frame. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	FVector GetListenerLocation() const { return ListenerLocation; }

	// --------------------------------------------------------------------------------------------------
	// Display
	// --------------------------------------------------------------------------------------------------

	/**
	 * Draw the counter box onto a canvas. This is the whole HUD renderer.
	 *
	 * Call it from your own HUD class: Service->DrawStats(Canvas); - one line, in DrawHUD. Or use
	 * ASoundVeilHUD, which does exactly that for you.
	 *
	 * The box is not decoration. Occlusion is the one effect that cannot be shown in a still image by
	 * pointing a camera at it, so the numbers are the only proof there is: traces against budget, the
	 * longest wait, and per source the occlusion, the volume factor and the cut-off in Hz.
	 */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Debug")
	void DrawStats(UCanvas* Canvas);

	/** Show or hide the counter box. Same as SoundVeil.Show. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Debug")
	void SetShowStats(bool bInShowStats) { bShowStats = bInShowStats; }

	/** True while the counter box is being drawn. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil|Debug")
	bool IsShowingStats() const { return bShowStats; }

	/** Write the current state of every source to the log. Same as SoundVeil.Stats. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil|Debug")
	void LogStats() const;

	/** The profile a source with no profile of its own ends up using. Never returns null. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	USoundVeilProfile* GetDefaultProfile();

private:
	/** One registered source. Flat, slot-allocated, and never a pointer anybody else holds. */
	struct FSourceEntry
	{
		TWeakObjectPtr<UAudioComponent> Audio;
		TWeakObjectPtr<USoundVeilComponent> Component;
		TWeakObjectPtr<USoundVeilProfile> Profile;
		TWeakObjectPtr<AActor> Owner;

		/** Where the sound is, sampled once per frame from the audio component. */
		FVector Location = FVector::ZeroVector;

		/**
		 * The source's own volume multiplier as it was at registration.
		 *
		 * Everything SoundVeil applies is a factor on top of this, never a replacement for it. Without
		 * it the second frame would read back the volume the first frame wrote and the source would
		 * fade to nothing all by itself.
		 */
		float BaseVolume = 1.0f;

		/** Smoothed occlusion. The value everything downstream uses. */
		float Occlusion = 0.0f;

		/** Raw occlusion from the last completed measurement. */
		float TargetOcclusion = 0.0f;

		float VolumeMultiplier = 1.0f;
		float LowpassFrequency = 20000.0f;

		/** Last values actually pushed into the audio component, so unchanged frames cost nothing. */
		float AppliedVolume = -1.0f;
		float AppliedLowpass = -1.0f;

		float Distance = 0.0f;
		float Priority = 1.0f;

		int32 FramesSinceUpdate = 0;
		int32 LastTapCount = 0;

		/** Traces in flight, started last frame and read back on this one. */
		TArray<FTraceHandle> Pending;
		int32 PendingTaps = 0;
		int32 PendingBlocked = 0;
		int32 PendingResolved = 0;
		int32 PendingFrames = 0;

		uint32 Serial = 0;

		bool bApplyVolume = true;
		bool bApplyLowpass = true;
		bool bAudible = true;
		bool bGrouped = false;
		bool bAlive = false;
	};

	/** Read the project settings into the runtime copies. Called once, on Initialize. */
	void ApplySettings();

	/** Slot allocation. */
	int32 AllocateSlot();
	void ReleaseSlot(int32 Slot);

	/** Slot behind a handle, or INDEX_NONE when the handle is stale. */
	int32 ResolveSlot(const FSoundVeilSourceHandle& Handle) const;

	/** Sample location, distance, loudness and audibility for every live source. O(N), every frame. */
	void RefreshSources();

	/** Work out where we are listening from this frame. */
	void UpdateListener();

	/** Read back the traces started on earlier frames and turn finished sets into a target occlusion. */
	void CollectTraceResults();

	/** Decide who is tracked individually, who is grouped, and build the round. Not every frame. */
	void RebuildSchedule();

	/** Spend this frame's trace budget on the next entries in the round. */
	void RunMeasurementRound();

	/** Start one measurement for a source. Returns how many traces it actually spent. */
	int32 MeasureSource(int32 Slot, int32 MaxTaps);

	/** Walk every source towards its target occlusion and push the result into its audio component. */
	void SmoothAndApply(float DeltaTime);

	/** Push one source's values into its audio component, if they moved. */
	void ApplyToAudio(FSourceEntry& Entry);

	/** Collision query parameters for a measurement: ignore the source's own actor and the listener's. */
	void BuildQueryParams(const FSourceEntry& Entry, struct FCollisionQueryParams& OutParams) const;

	/** Profile of a source, or the fallback. Never returns null. */
	USoundVeilProfile* EffectiveProfile(const FSourceEntry& Entry);

#if WITH_EDITOR
	/**
	 * Second attachment point for the counter box, editor only.
	 *
	 * The path that ships is AHUD::DrawHUD (see ASoundVeilHUD), because UDebugDrawService is compiled
	 * out of a cooked build entirely. This one exists so the numbers show up in a play-in-editor
	 * viewport without anyone having to set a HUD class first - which is where the store screenshots
	 * come from, and an audio plugin with no numbers in the screenshot cannot be shown at all.
	 */
	void OnEditorViewportDraw(UCanvas* Canvas, APlayerController* PlayerController);
#endif

	/** The source rows. Slots are reused through FreeSlots; the array never shrinks. */
	TArray<FSourceEntry> Sources;
	TArray<int32> FreeSlots;
	int32 LiveSourceCount = 0;

	/** Audio component to slot, so registering and unregistering stay O(1) at any source count. */
	TMap<TObjectKey<UAudioComponent>, int32> SourceLookup;

	/** Scratch for the ranking pass, reserved once and then only reset. */
	TArray<FSoundVeilSourceRank> RankScratch;
	TArray<int32> RankSlots;
	TArray<int32> TrackedScratch;
	TArray<int32> RoundScratch;
	TArray<FVector> TapStarts;
	TArray<FVector> TapEnds;

	/** The current scheduling round, as slot indices, and how far into it this frame got. */
	TArray<int32> RoundQueue;
	int32 RoundCursor = 0;

	/** Set when a registration, a budget change or a tap override invalidated the round. */
	bool bScheduleDirty = true;

	/** The one source that stands in for the whole grouped set on its turn in the round. */
	int32 GroupRepresentativeSlot = INDEX_NONE;

	/** Occlusion the grouped sources share, smoothed like any other. */
	float GroupOcclusion = 0.0f;
	float GroupTargetOcclusion = 0.0f;

	/** Summary of the grouped set, refreshed with the schedule. */
	FSoundVeilOverflowSummary GroupSummary;

	/** Where we are measuring from, and who to ignore while doing it. */
	FVector ListenerLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> ListenerActor;
	TWeakObjectPtr<AActor> ListenerOverride;

	/** Generation counter handed to the next allocated slot. Wraps harmlessly, never to zero. */
	uint32 NextSerial = 1;

	/** Runtime copies of the project settings. */
	int32 TraceBudgetPerFrame = 8;
	int32 DefaultTapCount = 5;
	int32 MaxTurnsPerSource = 3;
	int32 MaxSources = 128;
	int32 StatsSourceLines = 6;
	float DefaultAttackSeconds = 0.15f;
	float DefaultReleaseSeconds = 0.35f;
	float DefaultPatternRadius = 60.0f;
	bool bUseAsyncTraces = true;

	/** Tap count forced on every source. */
	int32 TapCountOverride = 5;
	bool bHasTapCountOverride = false;

	bool bBypass = false;
	bool bFrozen = false;
	bool bShowStats = false;

	/** Frame the last real HUD pass drew on, so the editor path never draws a second box on top. */
	uint64 LastHudDrawFrame = 0;

	/** Handle for the editor-only counter box draw. */
	FDelegateHandle EditorDrawHandle;

	/** The fallback profile, created on demand so the plugin works before any asset exists. */
	UPROPERTY()
	TObjectPtr<USoundVeilProfile> BuiltinProfile;

	/** The project's default profile asset once it has been resolved, or null if there is none. */
	UPROPERTY()
	TObjectPtr<USoundVeilProfile> ResolvedDefaultProfile;

	/** Published through GetStats(). */
	FSoundVeilStats Stats;
};
