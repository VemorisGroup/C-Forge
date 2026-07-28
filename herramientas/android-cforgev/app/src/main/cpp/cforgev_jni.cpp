// cforgev_jni.cpp — Bridge JNI entre Java/Kotlin y el interprete C-Forge
// Compilado por el NDK como libcforgev_jni.so
//
// Para usar: copiar cforgev.cpp del repo raiz al mismo directorio.

#include <jni.h>
#include <android/log.h>
#include <string>
#include <fstream>
#include <sstream>

#define LOG_TAG "CForge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Incluir el interprete C-Forge ────────────────────────────────────────
// NOTA: cforgev.cpp debe estar en el mismo directorio que este archivo.
// El NDK compila ambos archivos juntos via CMakeLists.txt.
// Las funciones de la C API (cfv_run_file, cfv_run_string, cfv_eval_json)
// estan definidas en cforgev.cpp con extern "C".

extern "C" {
    int         cfv_run_file(const char* path);
    int         cfv_run_string(const char* code);
    const char* cfv_eval_json(const char* code);
    const char* cfv_version();
}

// ── JNI: com.vemoris.cforgeapp.CForgeRuntime ────────────────────────────

extern "C" JNIEXPORT jstring JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeGetVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(cfv_version());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeRunFile(JNIEnv* env, jclass, jstring jpath) {
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    int result = cfv_run_file(path);
    env->ReleaseStringUTFChars(jpath, path);
    LOGI("cfv_run_file(%s) = %d", path, result);
    return (jboolean)(result == 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeRunCode(JNIEnv* env, jclass, jstring jcode) {
    const char* code = env->GetStringUTFChars(jcode, nullptr);
    int result = cfv_run_string(code);
    env->ReleaseStringUTFChars(jcode, code);
    return (jboolean)(result == 0);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeEvalJson(JNIEnv* env, jclass, jstring jcode) {
    const char* code = env->GetStringUTFChars(jcode, nullptr);
    const char* result = cfv_eval_json(code);
    env->ReleaseStringUTFChars(jcode, code);
    return env->NewStringUTF(result ? result : "null");
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeEvalFloat(JNIEnv* env, jclass, jstring jcode) {
    const char* code = env->GetStringUTFChars(jcode, nullptr);
    const char* result = cfv_eval_json(code);
    env->ReleaseStringUTFChars(jcode, code);
    return result ? (jfloat)std::stof(std::string(result)) : 0.0f;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vemoris_cforgeapp_CForgeRuntime_nativeEvalString(JNIEnv* env, jclass, jstring jcode) {
    const char* code = env->GetStringUTFChars(jcode, nullptr);
    const char* result = cfv_eval_json(code);
    env->ReleaseStringUTFChars(jcode, code);
    std::string s = result ? result : "";
    // Quitar comillas JSON si es string
    if (s.size() >= 2 && s.front()=='"' && s.back()=='"')
        s = s.substr(1, s.size()-2);
    return env->NewStringUTF(s.c_str());
}
