using UnrealBuildTool;

public class J2A : ModuleRules
{
	public J2A(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { });
			
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
				"EditorStyle",
				"ToolMenus",
				"DesktopPlatform",
				"ContentBrowser",
				"AssetRegistry"
			}
		);
		
		DynamicallyLoadedModuleNames.AddRange(new string[] { });
	}
}
