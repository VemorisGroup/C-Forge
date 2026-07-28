package com.vemoris.cforgeapp;

/**
 * CForgeRuntime — Wrapper Java para el interprete C-Forge via JNI.
 *
 * Uso:
 *   CForgeRuntime.runCode("mostrar(\"Hola Android!\")");
 *   String version = CForgeRuntime.getVersion();
 *   double result = CForgeRuntime.evalFloat("2 + 2");
 */
public class CForgeRuntime {

    static {
        // Carga la libreria nativa compilada por el NDK
        System.loadLibrary("cforgev_jni");
    }

    // ── Metodos nativos (implementados en cforgev_jni.cpp) ─────────────────

    private static native String  nativeGetVersion();
    private static native boolean nativeRunFile(String path);
    private static native boolean nativeRunCode(String code);
    private static native String  nativeEvalJson(String code);
    private static native float   nativeEvalFloat(String code);
    private static native String  nativeEvalString(String code);

    // ── API publica ────────────────────────────────────────────────────────

    /** Retorna la version del interprete, ej: "2.1.0" */
    public static String getVersion() {
        return nativeGetVersion();
    }

    /** Ejecuta un archivo .cfv desde la ruta dada. Retorna true si exito. */
    public static boolean runFile(String path) {
        return nativeRunFile(path);
    }

    /** Ejecuta codigo C-Forge en una cadena. Retorna true si exito. */
    public static boolean runCode(String code) {
        return nativeRunCode(code);
    }

    /** Evalua expresion C-Forge y retorna el resultado como JSON. */
    public static String evalJson(String expr) {
        return nativeEvalJson(expr);
    }

    /** Evalua expresion numerica C-Forge. Ej: evalFloat("2 + 2") == 4.0 */
    public static float evalFloat(String expr) {
        return nativeEvalFloat(expr);
    }

    /** Evalua expresion C-Forge y retorna texto (sin comillas JSON). */
    public static String evalString(String expr) {
        return nativeEvalString(expr);
    }

    /** Copia un asset .cfv a cache y lo ejecuta. */
    public static boolean runAsset(android.content.Context ctx, String assetName) {
        try {
            java.io.InputStream is = ctx.getAssets().open(assetName);
            java.io.File outFile = new java.io.File(ctx.getCacheDir(), assetName);
            outFile.getParentFile().mkdirs();
            java.io.FileOutputStream fos = new java.io.FileOutputStream(outFile);
            byte[] buf = new byte[4096];
            int len;
            while ((len = is.read(buf)) != -1) fos.write(buf, 0, len);
            fos.close(); is.close();
            return nativeRunFile(outFile.getAbsolutePath());
        } catch (Exception e) {
            android.util.Log.e("CForge", "runAsset failed: " + e.getMessage());
            return false;
        }
    }
}
