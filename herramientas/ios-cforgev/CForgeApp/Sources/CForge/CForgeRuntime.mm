// CForgeRuntime.mm — Bridge Objective-C++ entre Swift y el interprete C-Forge
// Este archivo combina ObjC++ (.mm) para poder incluir cforgev.cpp (C++20).
// El build system de Xcode compila ambos juntos.

#import "CForgeRuntime.h"
#include <string>
#include <stdexcept>

// ── C API del interprete (definida en cforgev.cpp) ────────────────────────
// Copiar cforgev.cpp al mismo directorio Sources/CForge/
extern "C" {
    int         cfv_run_file(const char* path);
    int         cfv_run_string(const char* code);
    const char* cfv_eval_json(const char* code);
    const char* cfv_version();
    int         cfv_has_openssl();
}

@implementation CForgeRuntime

// ── Version ───────────────────────────────────────────────────────────────

+ (NSString*)version {
    return [NSString stringWithUTF8String:cfv_version()];
}

+ (BOOL)hasOpenSSL {
    return cfv_has_openssl() == 1;
}

// ── Ejecucion ─────────────────────────────────────────────────────────────

+ (BOOL)runFile:(NSString*)path error:(NSError**)error {
    int result = cfv_run_file([path UTF8String]);
    if (result != 0 && error) {
        *error = [NSError errorWithDomain:@"CForge"
                                     code:result
                                 userInfo:@{NSLocalizedDescriptionKey: @"Error al ejecutar archivo"}];
    }
    return result == 0;
}

+ (BOOL)runCode:(NSString*)code error:(NSError**)error {
    int result = cfv_run_string([code UTF8String]);
    if (result != 0 && error) {
        *error = [NSError errorWithDomain:@"CForge"
                                     code:result
                                 userInfo:@{NSLocalizedDescriptionKey: @"Error al ejecutar codigo"}];
    }
    return result == 0;
}

// ── Evaluacion ────────────────────────────────────────────────────────────

+ (NSString*)evalJson:(NSString*)expr {
    const char* result = cfv_eval_json([expr UTF8String]);
    if (!result) return @"null";
    return [NSString stringWithUTF8String:result];
}

+ (double)evalNumber:(NSString*)expr {
    const char* result = cfv_eval_json([expr UTF8String]);
    if (!result) return 0.0;
    return [[NSString stringWithUTF8String:result] doubleValue];
}

+ (NSString*)evalString:(NSString*)expr {
    NSString* json = [self evalJson:expr];
    // Quitar comillas JSON si es string
    if (json.length >= 2 && [json hasPrefix:@"\""] && [json hasSuffix:@"\""]) {
        return [json substringWithRange:NSMakeRange(1, json.length - 2)];
    }
    return json;
}

// ── Cargar asset del bundle ───────────────────────────────────────────────

+ (BOOL)runBundleScript:(NSString*)scriptName error:(NSError**)error {
    NSString* path = [[NSBundle mainBundle] pathForResource:scriptName ofType:@"cfv"];
    if (!path) {
        if (error) {
            *error = [NSError errorWithDomain:@"CForge"
                                         code:-1
                                     userInfo:@{NSLocalizedDescriptionKey:
                                                    [NSString stringWithFormat:@"Script no encontrado: %@.cfv", scriptName]}];
        }
        return NO;
    }
    return [self runFile:path error:error];
}

@end
