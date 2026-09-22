// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SoundVeilLog.h"
#include "SoundVeilSubsystem.h"

USoundVeilComponent::USoundVeilComponent()
{
	// Nothing to tick. The service owns the round, the budget and the smoothing, and pushes a value in
	// when it has one - a component that polled every frame would put back exactly the per-source
	// per-frame cost the plugin exists to remove.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bWantsInitializeComponent = false;
}

void USoundVeilComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegister)
	{
		Register();
	}
}

void USoundVeilComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unregister();
	Super::EndPlay(EndPlayReason);
}

UAudioComponent* USoundVeilComponent::ResolveAudioComponent() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<UAudioComponent*> AudioComponents;
	Owner->GetComponents<UAudioComponent>(AudioComponents);

	if (AudioComponents.Num() == 0)
	{
		return nullptr;
	}

	if (AudioComponentName.IsNone())
	{
		return AudioComponents[0];
	}

	for (UAudioComponent* Audio : AudioComponents)
	{
		if (Audio && Audio->GetFName() == AudioComponentName)
		{
			return Audio;
		}
	}

	// Named but not found: fall back rather than silently doing nothing, and say so once.
	UE_LOG(LogSoundVeil, Warning,
		TEXT("SoundVeil: %s has no audio component called '%s'; occluding '%s' instead."),
		*Owner->GetName(), *AudioComponentName.ToString(),
		AudioComponents[0] ? *AudioComponents[0]->GetName() : TEXT("<none>"));

	return AudioComponents[0];
}

void USoundVeilComponent::Register()
{
	if (Handle.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	USoundVeilSubsystem* Service = World ? World->GetSubsystem<USoundVeilSubsystem>() : nullptr;
	if (!Service)
	{
		return;
	}

	UAudioComponent* Audio = ResolveAudioComponent();
	if (!Audio)
	{
		UE_LOG(LogSoundVeil, Warning,
			TEXT("SoundVeil: %s has a SoundVeil Source component but no audio component to occlude."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<no owner>"));
		return;
	}

	ResolvedAudio = Audio;
	Handle = Service->RegisterComponentSource(this, Audio);
}

void USoundVeilComponent::Unregister()
{
	if (!Handle.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (USoundVeilSubsystem* Service = World ? World->GetSubsystem<USoundVeilSubsystem>() : nullptr)
	{
		Service->UnregisterSource(Handle);
	}

	Handle.Reset();
	ResolvedAudio.Reset();
	CachedOcclusion = 0.0f;
	CachedVolume = 1.0f;
	CachedLowpass = 20000.0f;
	LastBroadcastOcclusion = -1.0f;
}

bool USoundVeilComponent::IsSourceRegistered() const
{
	const UWorld* World = GetWorld();
	const USoundVeilSubsystem* Service = World ? World->GetSubsystem<USoundVeilSubsystem>() : nullptr;
	return Service && Service->IsSourceRegistered(Handle);
}

void USoundVeilComponent::SetProfile(USoundVeilProfile* InProfile)
{
	if (Profile == InProfile)
	{
		return;
	}

	Profile = InProfile;

	// Re-registering is the cheapest way to get the new profile's priority into the schedule, and the
	// occlusion value survives it because the audio component - not this component - is the identity.
	if (Handle.IsValid())
	{
		Unregister();
		Register();
	}
}

void USoundVeilComponent::NotifyOcclusion(float InOcclusion, float InVolume, float InLowpass)
{
	CachedOcclusion = InOcclusion;
	CachedVolume = InVolume;
	CachedLowpass = InLowpass;

	if (LastBroadcastOcclusion < 0.0f
		|| FMath::Abs(InOcclusion - LastBroadcastOcclusion) >= FMath::Max(OcclusionChangeThreshold, KINDA_SMALL_NUMBER))
	{
		LastBroadcastOcclusion = InOcclusion;
		OnOcclusionChanged.Broadcast(InOcclusion);
	}
}
