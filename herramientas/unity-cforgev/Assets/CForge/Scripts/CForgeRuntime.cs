// CForgeRuntime.cs — P/Invoke wrapper para el interprete C-Forge en Unity
// Coloca libcforgev.bundle/.so/.dll en Assets/CForge/Plugins/
using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace CForge
{
    /// <summary>
    /// Wrapper de bajo nivel para la C API de cforgev.
    /// Usa CForgeComponent o CForgeManager para acceso de alto nivel.
    /// </summary>
    public static class CForgeRuntime
    {
        private const string LIB = "cforgev";

        // int cfv_run_file(const char* path)
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern int cfv_run_file([MarshalAs(UnmanagedType.LPStr)] string path);

        // int cfv_run_string(const char* code)
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern int cfv_run_string([MarshalAs(UnmanagedType.LPStr)] string code);

        // const char* cfv_eval_json(const char* code)
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr cfv_eval_json([MarshalAs(UnmanagedType.LPStr)] string code);

        // const char* cfv_version()
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr cfv_version();

        // int cfv_has_sdl2()
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern int cfv_has_sdl2();

        // int cfv_has_openssl()
        [DllImport(LIB, CallingConvention = CallingConvention.Cdecl)]
        private static extern int cfv_has_openssl();

        // ── API publica ────────────────────────────────────────────────────

        /// <summary>Ejecutar un archivo .cfv. Retorna true si exitoso.</summary>
        public static bool RunFile(string path)
        {
            try { return cfv_run_file(path) == 0; }
            catch (Exception e) { Debug.LogError($"[C-Forge] RunFile error: {e.Message}"); return false; }
        }

        /// <summary>Ejecutar codigo C-Forge en linea. Retorna true si exitoso.</summary>
        public static bool RunCode(string code)
        {
            try { return cfv_run_string(code) == 0; }
            catch (Exception e) { Debug.LogError($"[C-Forge] RunCode error: {e.Message}"); return false; }
        }

        /// <summary>Evaluar expresion C-Forge y retornar resultado como JSON string.</summary>
        public static string EvalJson(string code)
        {
            try
            {
                IntPtr ptr = cfv_eval_json(code);
                return ptr != IntPtr.Zero ? Marshal.PtrToStringAnsi(ptr) : "null";
            }
            catch (Exception e) { return $"{{\"error\":\"{e.Message}\"}}"; }
        }

        /// <summary>Evaluar expresion y retornar como float.</summary>
        public static float EvalFloat(string code)
        {
            string json = EvalJson(code);
            return float.TryParse(json, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out float v) ? v : 0f;
        }

        /// <summary>Evaluar expresion y retornar como string (sin comillas JSON).</summary>
        public static string EvalString(string code)
        {
            string json = EvalJson(code);
            if (json.StartsWith("\"") && json.EndsWith("\""))
                return json.Substring(1, json.Length - 2);
            return json;
        }

        /// <summary>Llamar funcion C-Forge sin argumentos.</summary>
        public static string Call(string funcName) => EvalJson(funcName + "()");

        /// <summary>Llamar funcion con argumento float.</summary>
        public static string Call(string funcName, float arg) =>
            EvalJson($"{funcName}({arg.ToString(System.Globalization.CultureInfo.InvariantCulture)})");

        /// <summary>Llamar funcion con argumento string.</summary>
        public static string Call(string funcName, string arg) =>
            EvalJson($"{funcName}(\"{arg.Replace("\"", "\\\"")}\")");

        /// <summary>Llamar funcion con dos argumentos float.</summary>
        public static string Call(string funcName, float a, float b) =>
            EvalJson($"{funcName}({a.ToString(System.Globalization.CultureInfo.InvariantCulture)}, {b.ToString(System.Globalization.CultureInfo.InvariantCulture)})");

        /// <summary>Version del interprete C-Forge.</summary>
        public static string Version()
        {
            try { return Marshal.PtrToStringAnsi(cfv_version()) ?? "?"; }
            catch { return "?"; }
        }

        /// <summary>True si el interprete fue compilado con SDL2.</summary>
        public static bool HasSDL2() { try { return cfv_has_sdl2() != 0; } catch { return false; } }

        /// <summary>True si el interprete fue compilado con OpenSSL.</summary>
        public static bool HasOpenSSL() { try { return cfv_has_openssl() != 0; } catch { return false; } }
    }
}
