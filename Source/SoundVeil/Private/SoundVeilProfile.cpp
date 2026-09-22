// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SoundVeilProfile.h"

USoundVeilProfile::USoundVeilProfile()
{
	ResetCurvesToDefaults();
}

void USoundVeilProfile::ResetCurvesToDefaults()
{
	// A brick wall, roughly. Two keys each: the point of the curves is that a sound designer moves
	// them, so the defaults only have to be a believable starting shape, not a measurement.
	FRichCurve& Volume = *OcclusionToVolume.GetRichCurve();
	Volume.Reset();
	Volume.AddKey(0.0f, 1.0f);
	Volume.AddKey(0.5f, 0.62f);
	Volume.AddKey(1.0f, 0.25f);

	FRichCurve& Lowpass = *OcclusionToLowpass.GetRichCurve();
	Lowpass.Reset();
	Lowpass.AddKey(0.0f, MaxLowpassFrequency);
	// Halfway through, most of the top is already gone: the ear reads high frequency loss as "covered"
	// long before it reads volume loss as anything but "further away".
	Lowpass.AddKey(0.5f, 3500.0f);
	Lowpass.AddKey(1.0f, 700.0f);
}

float USoundVeilProfile::EvaluateVolume(float Occlusion) const
{
	const float X = FMath::Clamp(Occlusion, 0.0f, 1.0f);
	const FRichCurve* Curve = OcclusionToVolume.GetRichCurveConst();
	const float Value = (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(X, 1.0f) : (1.0f - 0.75f * X);
	return FMath::Clamp(Value, 0.0f, 1.0f);
}

float USoundVeilProfile::EvaluateLowpass(float Occlusion) const
{
	const float X = FMath::Clamp(Occlusion, 0.0f, 1.0f);
	const FRichCurve* Curve = OcclusionToLowpass.GetRichCurveConst();
	const float Value = (Curve && Curve->GetNumKeys() > 0)
		? Curve->Eval(X, MaxLowpassFrequency)
		: FMath::Lerp(MaxLowpassFrequency, 700.0f, X);
	return FMath::Clamp(Value, MinLowpassFrequency, MaxLowpassFrequency);
}
