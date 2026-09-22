// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class SoundVeil : ModuleRules
{
	public SoundVeil(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// Deliberately NOT here:
		//   UnrealEd - everything in this plugin ships. The editor-only viewport draw is the one
		//              exception and it lives behind WITH_EDITOR inside the runtime module.
		//   UMG      - the counter box is UCanvas from AHUD, so it survives a cooked Shipping build.
		//              A UMG box would be the first thing to disappear from a packaged demo.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// AudioMixer / AudioExtensions: the source values are handed to UAudioComponent, which is
			// Engine, but keeping the audio modules linked means the plugin can grow into the mixer
			// side (submix sends, source effects) without a dependency change breaking anyone's build.
			"AudioMixer",
			"AudioExtensions",

			// PhysicsCore: the collision channel a profile traces on comes from here.
			"PhysicsCore",

			// RenderCore: GWhiteTexture, which the counter box background is drawn with.
			"RenderCore",
		});
	}
}
