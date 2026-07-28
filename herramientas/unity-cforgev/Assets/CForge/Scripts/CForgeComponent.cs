// CForgeComponent.cs — MonoBehaviour para scripting C-Forge en Unity
// Agrega este componente a cualquier GameObject y asigna scriptPath en el Inspector.
using UnityEngine;

namespace CForge
{
    /// <summary>
    /// MonoBehaviour que carga y ejecuta un script C-Forge (.cfv).
    /// Llama automaticamente on_inicio(), on_actualizar(delta), on_colision(nombre),
    /// on_destruir() en los eventos de Unity correspondientes.
    /// </summary>
    public class CForgeComponent : MonoBehaviour
    {
        [Header("C-Forge Script")]
        [Tooltip("Ruta al .cfv relativa a StreamingAssets/")]
        public string scriptPath = "Scripts/mi_script.cfv";

        [Tooltip("Recargar script cada vez que se activa (util para desarrollo)")]
        public bool reloadOnEnable = true;

        [Tooltip("Ejecutar on_actualizar cada frame")]
        public bool tickEnabled = true;

        private bool scriptLoaded = false;

        // ── Unity Lifecycle ────────────────────────────────────────────────

        void Start()
        {
            if (!string.IsNullOrEmpty(scriptPath))
                LoadScript();
        }

        void OnEnable()
        {
            if (reloadOnEnable && scriptLoaded)
                LoadScript();
        }

        void Update()
        {
            if (scriptLoaded && tickEnabled)
                CForgeRuntime.Call("on_actualizar", Time.deltaTime);
        }

        void OnDestroy()
        {
            if (scriptLoaded)
                CForgeRuntime.Call("on_destruir");
        }

        void OnCollisionEnter(Collision collision)
        {
            if (scriptLoaded)
                CForgeRuntime.Call("on_colision", collision.gameObject.name);
        }

        void OnTriggerEnter(Collider other)
        {
            if (scriptLoaded)
                CForgeRuntime.Call("on_trigger", other.gameObject.name);
        }

        // ── API publica ────────────────────────────────────────────────────

        /// <summary>Cargar y ejecutar el script asignado.</summary>
        public bool LoadScript()
        {
            string fullPath = System.IO.Path.Combine(Application.streamingAssetsPath, scriptPath);
            scriptLoaded = CForgeRuntime.RunFile(fullPath);
            if (scriptLoaded)
            {
                CForgeRuntime.Call("on_inicio");
                Debug.Log($"[C-Forge] Script cargado: {scriptPath}");
            }
            else
            {
                Debug.LogError($"[C-Forge] Error cargando: {scriptPath}");
            }
            return scriptLoaded;
        }

        /// <summary>Ejecutar codigo C-Forge en linea desde C#.</summary>
        public bool RunCode(string code) => CForgeRuntime.RunCode(code);

        /// <summary>Llamar funcion C-Forge sin argumentos.</summary>
        public string Call(string funcName) => CForgeRuntime.Call(funcName);

        /// <summary>Llamar funcion con argumento float.</summary>
        public string Call(string funcName, float arg) => CForgeRuntime.Call(funcName, arg);

        /// <summary>Llamar funcion con argumento string.</summary>
        public string Call(string funcName, string arg) => CForgeRuntime.Call(funcName, arg);

        /// <summary>Obtener variable numerica del script.</summary>
        public float GetFloat(string varName) => CForgeRuntime.EvalFloat(varName);

        /// <summary>Obtener variable de texto del script.</summary>
        public string GetString(string varName) => CForgeRuntime.EvalString(varName);

        /// <summary>Asignar variable numerica en el script.</summary>
        public void SetFloat(string varName, float value) =>
            CForgeRuntime.RunCode($"{varName} = {value.ToString(System.Globalization.CultureInfo.InvariantCulture)}");

        /// <summary>Asignar variable de texto en el script.</summary>
        public void SetString(string varName, string value) =>
            CForgeRuntime.RunCode($"{varName} = \"{value.Replace("\"", "\\\"")}\"");
    }
}
