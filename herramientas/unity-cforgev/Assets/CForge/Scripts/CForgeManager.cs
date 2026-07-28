// CForgeManager.cs — Singleton global para C-Forge en Unity
// Usar para ejecutar scripts globales, inicializar el runtime, etc.
using UnityEngine;

namespace CForge
{
    /// <summary>
    /// Singleton global para gestionar el runtime de C-Forge.
    /// Coloca un GameObject con este componente en tu escena inicial.
    /// </summary>
    public class CForgeManager : MonoBehaviour
    {
        public static CForgeManager Instance { get; private set; }

        [Header("Configuracion")]
        [Tooltip("Script de inicializacion global (se ejecuta al inicio)")]
        public string globalInitScript = "";

        [Tooltip("Mostrar version del interprete en consola al inicio")]
        public bool logVersion = true;

        void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);
            Initialize();
        }

        private void Initialize()
        {
            if (logVersion)
                Debug.Log($"[C-Forge] Interprete v{CForgeRuntime.Version()} | SDL2={CForgeRuntime.HasSDL2()} | OpenSSL={CForgeRuntime.HasOpenSSL()}");

            if (!string.IsNullOrEmpty(globalInitScript))
            {
                string path = System.IO.Path.Combine(Application.streamingAssetsPath, globalInitScript);
                bool ok = CForgeRuntime.RunFile(path);
                if (!ok)
                    Debug.LogError($"[C-Forge] Error en script global: {globalInitScript}");
            }
        }

        /// <summary>Ejecutar codigo C-Forge desde cualquier parte del proyecto.</summary>
        public static bool Run(string code) => CForgeRuntime.RunCode(code);

        /// <summary>Ejecutar archivo .cfv desde StreamingAssets/.</summary>
        public static bool RunFile(string relativePath)
        {
            string full = System.IO.Path.Combine(Application.streamingAssetsPath, relativePath);
            return CForgeRuntime.RunFile(full);
        }

        /// <summary>Evaluar expresion C-Forge y obtener float.</summary>
        public static float EvalFloat(string expr) => CForgeRuntime.EvalFloat(expr);

        /// <summary>Evaluar expresion C-Forge y obtener string.</summary>
        public static string EvalString(string expr) => CForgeRuntime.EvalString(expr);
    }
}
