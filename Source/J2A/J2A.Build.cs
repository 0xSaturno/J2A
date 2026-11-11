using UnrealBuildTool;

public class J2A : ModuleRules
{
	public J2A(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetTools",
				"Json",
				"JsonUtilities",
				"ToolMenus",
				"DesktopPlatform",
				"ContentBrowser",
				"ContentBrowserData",
				"AssetRegistry",
				"MaterialEditor"
			}
		);
	}
}
