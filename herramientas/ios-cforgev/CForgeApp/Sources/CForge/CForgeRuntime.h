// CForgeRuntime.h — Header ObjC para el runtime C-Forge en iOS
// Importar desde Swift: @_silgen_name o via Bridging Header

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * CForgeRuntime — Interfaz ObjC que Swift puede usar directamente.
 *
 * Uso desde Swift:
 *   let v = CForgeRuntime.version()
 *   CForgeRuntime.runCode("mostrar(\"Hola iOS!\")", error: nil)
 *   let r = CForgeRuntime.evalNumber("2 + 2")  // 4.0
 */
@interface CForgeRuntime : NSObject

/// Version del interprete, ej: "2.1.0"
+ (NSString*)version;

/// true si compilado con OpenSSL
+ (BOOL)hasOpenSSL;

/// Ejecuta un archivo .cfv. Retorna YES si exito.
+ (BOOL)runFile:(NSString*)path error:(NSError* _Nullable*)error;

/// Ejecuta codigo C-Forge. Retorna YES si exito.
+ (BOOL)runCode:(NSString*)code error:(NSError* _Nullable*)error;

/// Evalua expresion y retorna JSON. Ej: evalJson("2+2") -> "4.0"
+ (NSString*)evalJson:(NSString*)expr;

/// Evalua expresion numerica. Ej: evalNumber("2+2") -> 4.0
+ (double)evalNumber:(NSString*)expr;

/// Evalua expresion y retorna texto (sin comillas JSON).
+ (NSString*)evalString:(NSString*)expr;

/// Carga y ejecuta un script del app bundle (sin extension .cfv).
+ (BOOL)runBundleScript:(NSString*)scriptName error:(NSError* _Nullable*)error;

@end

NS_ASSUME_NONNULL_END
