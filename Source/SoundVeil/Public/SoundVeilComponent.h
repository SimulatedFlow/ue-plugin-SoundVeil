// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SoundVeilTypes.h"
#include "SoundVeilComponent.generated.h"

class UAudioComponent;
class USoundVeilProfile;
class USoundVeilSubsystem;

/** Fires when a source's occlusion moves by more than Occlusion Change Threshold. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSoundVeilOcclusionChangedSignature, float, Occlusion);

/**
 * Put this next to an audio component and the sound is occluded. That is the whole component.
 *
 * It registers the audio component with the world's service on Begin Play, hands back its own volume
 * on End Play, and forwards the occlusion value to Blueprint so a mesh, a material or a light can be
 * driven by it as well. It does not tick: the service pushes values in when they change, which for a
 * source in a quiet corner of the level is almost never.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent, DisplayName = "SoundVeil Source"))
class SOUNDVEIL_API USoundVeilComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USoundVeilComponent();

	// UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --------------------------------------------------------------------------------------------------
	// Setup
	// --------------------------------------------------------------------------------------------------

	/** How this source's material sounds. Empty falls back to the project's default profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil")
	TObjectPtr<USoundVeilProfile> Profile;

	/**
	 * Which audio component on this actor to occlude. Empty takes the first one found.
	 *
	 * Worth setting on an actor with several: a machine with a hum and an alarm may well want the hum
	 * occluded through concrete and the alarm through nothing at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil")
	FName AudioComponentName;

	/** Register on Begin Play. Off means you call Register yourself, when the sound actually starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil")
	bool bAutoRegister = true;

	/** Let the service drive the audio component's volume multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil")
	bool bApplyVolume = true;

	/**
	 * Let the service drive the audio component's low pass filter.
	 *
	 * Turn this off and keep the volume if your source already runs through a submix effect that owns
	 * the filtering - the occlusion value is still reported, it is just not applied here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil")
	bool bApplyLowpass = true;

	/** How far the occlusion must move before On Occlusion Changed fires. Keeps Blueprint work rare. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SoundVeil", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OcclusionChangeThreshold = 0.02f;

	/**
	 * Occlusion moved.
	 *
	 * This is what the demo columns are coloured from: occlusion is the one thing a screenshot cannot
	 * show by pointing a camera at it, so the demo puts the number on the geometry as brightness.
	 */
	UPROPERTY(BlueprintAssignable, Category = "SoundVeil")
	FSoundVeilOcclusionChangedSignature OnOcclusionChanged;

	// --------------------------------------------------------------------------------------------------
	// Control
	// --------------------------------------------------------------------------------------------------

	/** Register with the world's service now. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void Register();

	/** Unregister and hand the audio component its own volume back. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void Unregister();

	/**
	 * True while this source is under the service's care.
	 *
	 * Not called IsRegistered: UActorComponent already has one of those and it means something else
	 * entirely - whether the component itself is registered with the world. Shadowing it compiles and
	 * then answers the wrong question at the worst possible moment.
	 */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	bool IsSourceRegistered() const;

	/** Swap the profile at runtime - a door that changes what it is made of, or a debug button. */
	UFUNCTION(BlueprintCallable, Category = "SoundVeil")
	void SetProfile(USoundVeilProfile* InProfile);

	/** Smoothed occlusion, 0 = clear line, 1 = fully covered. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	float GetOcclusion() const { return CachedOcclusion; }

	/** Volume multiplier the occlusion curve last produced, 0..1. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	float GetVolumeMultiplier() const { return CachedVolume; }

	/** Low pass cut-off the occlusion curve last produced, in Hz. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	float GetLowpassFrequency() const { return CachedLowpass; }

	/** The audio component being occluded, resolved once at registration. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	UAudioComponent* GetOccludedAudioComponent() const { return ResolvedAudio.Get(); }

	/** The service's handle for this source. Invalid while unregistered. */
	UFUNCTION(BlueprintPure, Category = "SoundVeil")
	FSoundVeilSourceHandle GetSourceHandle() const { return Handle; }

	/** Called by the service when it has a new value. Not meant to be called from anywhere else. */
	void NotifyOcclusion(float InOcclusion, float InVolume, float InLowpass);

private:
	/** Find the audio component named by Audio Component Name, or the first one on the owner. */
	UAudioComponent* ResolveAudioComponent() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<UAudioComponent> ResolvedAudio;

	FSoundVeilSourceHandle Handle;

	float CachedOcclusion = 0.0f;
	float CachedVolume = 1.0f;
	float CachedLowpass = 20000.0f;

	/** Occlusion at the last broadcast, so the delegate fires on movement rather than every frame. */
	float LastBroadcastOcclusion = -1.0f;
};
