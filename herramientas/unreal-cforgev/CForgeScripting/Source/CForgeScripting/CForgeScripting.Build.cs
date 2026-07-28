// CForgeScripting.Build.cs — Reglas de compilacion para el plugin C-Forge en UE5
using UnrealBuildTool;
using System.IO;

public class CForgeScripting : ModuleRules
{
    public CForgeScripting(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine"
        });

        // Ruta a la shared library de C-Forge
        string PluginDir = Path.GetDirectoryName(RulesCompiler.GetFileNameFromType(GetType()));
        string BinDir    = Path.Combine(PluginDir, "../../Binaries");

        if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicAdditionalLibraries.Add(Path.Combine(BinDir, "cforgev.lib"));
            RuntimeDependencies.Add(Path.Combine(BinDir, "cforgev.dll"));
        } else if (Target.Platform == UnrealTargetPlatform.Mac) {
            PublicAdditionalLibraries.Add(Path.Combine(BinDir, "libcforgev.dylib"));
            RuntimeDependencies.Add(Path.Combine(BinDir, "libcforgev.dylib"));
        } else {
            // Linux
            PublicAdditionalLibraries.Add(Path.Combine(BinDir, "libcforgev.so"));
            RuntimeDependencies.Add(Path.Combine(BinDir, "libcforgev.so"));
        }

        PublicIncludePaths.Add(Path.Combine(PluginDir, "Public"));
    }
}
