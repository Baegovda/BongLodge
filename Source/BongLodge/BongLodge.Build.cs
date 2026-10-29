using UnrealBuildTool;

public class BongLodge : ModuleRules
{
	public BongLodge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore", "Slate", "SlateCore", "InputCore", "MainFrame", "Json"
		});
	}
}
