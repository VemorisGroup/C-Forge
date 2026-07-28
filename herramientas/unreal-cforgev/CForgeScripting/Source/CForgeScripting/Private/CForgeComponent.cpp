#include "CForgeComponent.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

UCForgeComponent::UCForgeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UCForgeComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!ScriptPath.IsEmpty() && (bReloadOnPlay || !bScriptLoaded))
    {
        FString FullPath = FPaths::ProjectContentDir() + ScriptPath;
        LoadScript(FullPath);
        // Llamar on_inicio si existe
        CallFunction(TEXT("on_inicio"));
    }
}

void UCForgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    CallFunction(TEXT("on_fin"));
}

void UCForgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bScriptLoaded)
    {
        CallFunctionWithFloat(TEXT("on_actualizar"), DeltaTime);
    }
}

bool UCForgeComponent::LoadScript(const FString& Path)
{
    std::string path_str = TCHAR_TO_UTF8(*Path);
    int result = cfv_run_file(path_str.c_str());
    bScriptLoaded = (result == 0);
    CurrentScriptPath = Path;
    if (!bScriptLoaded)
    {
        UE_LOG(LogTemp, Error, TEXT("[C-Forge] Error cargando script: %s"), *Path);
    }
    return bScriptLoaded;
}

bool UCForgeComponent::RunCode(const FString& Code)
{
    std::string code_str = TCHAR_TO_UTF8(*Code);
    return cfv_run_string(code_str.c_str()) == 0;
}

FString UCForgeComponent::CallFunction(const FString& FunctionName)
{
    FString code = FunctionName + TEXT("()");
    std::string code_str = TCHAR_TO_UTF8(*code);
    const char* result = cfv_eval_json(code_str.c_str());
    return FString(UTF8_TO_TCHAR(result));
}

FString UCForgeComponent::CallFunctionWithFloat(const FString& FunctionName, float Arg)
{
    FString code = FunctionName + TEXT("(") + FString::SanitizeFloat(Arg) + TEXT(")");
    std::string code_str = TCHAR_TO_UTF8(*code);
    const char* result = cfv_eval_json(code_str.c_str());
    return FString(UTF8_TO_TCHAR(result));
}

FString UCForgeComponent::CallFunctionWithString(const FString& FunctionName, const FString& Arg)
{
    FString code = FunctionName + TEXT("(\"") + Arg + TEXT("\")");
    std::string code_str = TCHAR_TO_UTF8(*code);
    const char* result = cfv_eval_json(code_str.c_str());
    return FString(UTF8_TO_TCHAR(result));
}

float UCForgeComponent::GetNumber(const FString& VarName)
{
    std::string code_str = TCHAR_TO_UTF8(*VarName);
    const char* result = cfv_eval_json(code_str.c_str());
    return FCString::Atof(UTF8_TO_TCHAR(result));
}

FString UCForgeComponent::GetString(const FString& VarName)
{
    FString code = TEXT("a_texto(") + VarName + TEXT(")");
    std::string code_str = TCHAR_TO_UTF8(*code);
    const char* result = cfv_eval_json(code_str.c_str());
    return FString(UTF8_TO_TCHAR(result));
}

FString UCForgeComponent::GetInterpreterVersion()
{
    return FString(UTF8_TO_TCHAR(cfv_version()));
}
