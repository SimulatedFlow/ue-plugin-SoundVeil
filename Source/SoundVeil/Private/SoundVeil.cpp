// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeil.h"
#include "SoundVeilLog.h"

DEFINE_LOG_CATEGORY(LogSoundVeil);

#define LOCTEXT_NAMESPACE "FSoundVeilModule"

void FSoundVeilModule::StartupModule()
{
	UE_LOG(LogSoundVeil, Log, TEXT("SoundVeil started."));
}

void FSoundVeilModule::ShutdownModule()
{
	UE_LOG(LogSoundVeil, Log, TEXT("SoundVeil shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSoundVeilModule, SoundVeil)
