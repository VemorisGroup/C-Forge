#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CForgeComponent.generated.h"

// C API de cforgev (shared library)
extern "C" {
    int         cfv_run_file(const char* path);
    int         cfv_run_string(const char* code);
    const char* cfv_eval_json(const char* code);
    const char* cfv_version();
}

/**
 * UCForgeComponent — Componente de scripting C-Forge para Actors de UE5.
 *
 * Uso:
 *   1. Agrega este componente a cualquier Actor.
 *   2. Asigna ScriptPath al archivo .cfv que quieres ejecutar.
 *   3. El script se carga en BeginPlay. Puedes llamar funciones con CallFunction().
 *
 * Ejemplo de script (enemigo.cfv):
 *   sea vida = 100
 *   funcion on_atacar(dano) { vida -= dano }
 *   funcion on_actualizar(delta) { ... }
 */
UCLASS(ClassGroup=(Scripting), meta=(BlueprintSpawnableComponent))
class CFORGESCRIPTING_API UCForgeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UCForgeComponent();

    /** Ruta al archivo .cfv (relativa a Content/) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="C-Forge")
    FString ScriptPath;

    /** Si verdadero, recarga el script en cada BeginPlay (util para desarrollo) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="C-Forge")
    bool bReloadOnPlay = true;

    /** Cargar y ejecutar el script */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    bool LoadScript(const FString& Path);

    /** Ejecutar codigo C-Forge en linea */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    bool RunCode(const FString& Code);

    /** Llamar una funcion C-Forge sin argumentos */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    FString CallFunction(const FString& FunctionName);

    /** Llamar funcion con un argumento numerico */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    FString CallFunctionWithFloat(const FString& FunctionName, float Arg);

    /** Llamar funcion con un argumento de texto */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    FString CallFunctionWithString(const FString& FunctionName, const FString& Arg);

    /** Obtener variable numerica del script */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    float GetNumber(const FString& VarName);

    /** Obtener variable de texto del script */
    UFUNCTION(BlueprintCallable, Category="C-Forge")
    FString GetString(const FString& VarName);

    /** Version del interprete C-Forge */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="C-Forge")
    static FString GetInterpreterVersion();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool bScriptLoaded = false;
    FString CurrentScriptPath;
};
