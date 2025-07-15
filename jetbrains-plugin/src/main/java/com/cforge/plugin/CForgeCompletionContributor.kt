package com.cforge.plugin

import com.intellij.codeInsight.completion.*
import com.intellij.codeInsight.lookup.LookupElementBuilder
import com.intellij.patterns.PlatformPatterns
import com.intellij.util.ProcessingContext

// ── Autocompletado ────────────────────────────────────────────────────────────
class CForgeCompletionContributor : CompletionContributor() {
    init {
        extend(CompletionType.BASIC,
            PlatformPatterns.psiElement(),
            CForgeCompletionProvider()
        )
    }
}

class CForgeCompletionProvider : CompletionProvider<CompletionParameters>() {

    private val keywords = listOf(
        "sea" to "declarar variable",
        "funcion" to "declarar función",
        "retornar" to "retornar valor",
        "si" to "condicional",
        "sino" to "rama else",
        "esi" to "else if",
        "para" to "bucle for-each",
        "en" to "iterador",
        "mientras" to "bucle while",
        "segun" to "switch/match",
        "caso" to "caso de segun",
        "romper" to "romper bucle",
        "continuar" to "continuar bucle",
        "clase" to "declarar clase",
        "nuevo" to "instanciar clase",
        "importar" to "importar módulo",
        "lanzar" to "lanzar excepción",
        "intentar" to "bloque try",
        "capturar" to "capturar excepción",
        "finalmente" to "bloque finally",
        "verdadero" to "booleano true",
        "falso" to "booleano false",
        "nulo" to "valor nulo",
        "y" to "operador AND",
        "o" to "operador OR",
        "no" to "operador NOT"
    )

    private val builtins = listOf(
        "mostrar" to "imprimir a consola",
        "tipo_de" to "obtener tipo del valor",
        "longitud" to "longitud de lista/texto",
        "agregar" to "agregar elemento a lista",
        "eliminar" to "eliminar elemento de lista",
        "insertar" to "insertar en posición",
        "ordenar" to "ordenar lista",
        "invertir" to "invertir lista",
        "filtrar" to "filtrar lista",
        "mapear" to "transformar lista",
        "reducir" to "reducir lista a valor",
        "texto_a_numero" to "convertir texto a número",
        "numero_a_texto" to "convertir número a texto",
        "texto_dividir" to "dividir texto por separador",
        "texto_unir" to "unir lista con separador",
        "texto_reemplazar" to "reemplazar en texto",
        "texto_contiene" to "verificar si texto contiene",
        "texto_mayusculas" to "convertir a mayúsculas",
        "texto_minusculas" to "convertir a minúsculas",
        "texto_trim" to "quitar espacios",
        "texto_empieza_con" to "verificar inicio",
        "texto_termina_con" to "verificar fin",
        "json_parsear" to "parsear JSON",
        "json_texto" to "convertir a JSON",
        "piso" to "redondear hacia abajo",
        "techo" to "redondear hacia arriba",
        "redondear" to "redondear",
        "absoluto" to "valor absoluto",
        "potencia" to "elevar a potencia",
        "raiz" to "raíz cuadrada",
        "maximo" to "valor máximo",
        "minimo" to "valor mínimo",
        "aleatorio" to "número aleatorio",
        "tiempo_ms" to "tiempo en milisegundos",
        "timestamp" to "timestamp Unix",
        "dormir" to "pausar ejecución",
        "env_obtener" to "obtener variable de entorno",
        "leer_archivo" to "leer archivo",
        "escribir_archivo" to "escribir archivo",
        "existe_archivo" to "verificar si archivo existe",
        "http_get" to "HTTP GET",
        "http_post" to "HTTP POST",
        "db_query" to "consulta SQL",
        "db_exec" to "ejecutar SQL",
        "mapa_claves" to "obtener claves del mapa",
        "mapa_valores" to "obtener valores del mapa",
        "mapa_entradas" to "obtener pares clave-valor",
        "mapa_fusionar" to "fusionar mapas",
        "tiene_clave" to "verificar si mapa tiene clave",
        "hilo_crear" to "crear hilo",
        "mutex_crear" to "crear mutex",
        "canal_crear" to "crear canal",
        "canal_enviar" to "enviar por canal",
        "canal_recibir" to "recibir del canal",
        "rango" to "generar rango numérico"
    )

    private val snippets = listOf(
        "funcion" to "funcion nombre(param: tipo): tipo {\n    \n}",
        "si" to "si (condicion) {\n    \n}",
        "para" to "para elemento en lista {\n    \n}",
        "mientras" to "mientras (condicion) {\n    \n}",
        "segun" to "segun valor {\n    caso \"x\": {\n        \n    }\n}",
        "clase" to "clase NombreClase {\n    funcion constructor(params) {\n        \n    }\n}",
        "intentar" to "intentar {\n    \n} capturar (e) {\n    mostrar(e)\n}",
        "importar" to "importar \"modulo\"",
        "prueba" to "prueba(\"descripción\", funcion() {\n    afirmar_igual(actual, esperado)\n})"
    )

    private val stdlib_modules = listOf(
        "matematica", "texto", "lista", "mapa", "fecha", "archivo", "io",
        "json", "csv", "yaml", "http_cliente", "framework", "orm", "db",
        "crypto", "base64", "regex", "log", "config", "cache", "email",
        "eventos", "esquema", "benchmark", "hilos", "concurrencia", "cli",
        "errores", "pruebas", "async", "enum", "interfaz", "tipado",
        "generadores", "ffi", "colecciones", "aleatorio", "plantilla",
        "validar", "router", "web"
    )

    override fun addCompletions(
        parameters: CompletionParameters,
        context: ProcessingContext,
        result: CompletionResultSet
    ) {
        // Palabras clave
        for ((kw, desc) in keywords) {
            result.addElement(
                LookupElementBuilder.create(kw)
                    .withTypeText(desc)
                    .withBoldness(true)
                    .withIcon(CForgeFileType.ICON)
            )
        }

        // Builtins
        for ((name, desc) in builtins) {
            result.addElement(
                LookupElementBuilder.create(name)
                    .withTypeText("builtin — $desc")
                    .withTailText("(...)")
                    .withIcon(CForgeFileType.ICON)
            )
        }

        // Tipos
        val types = listOf("texto", "numero", "booleano", "lista", "mapa",
                           "nulo", "cualquiera", "entero", "tupla", "conjunto")
        for (type in types) {
            result.addElement(
                LookupElementBuilder.create(type)
                    .withTypeText("tipo")
                    .withBoldness(false)
                    .withIcon(CForgeFileType.ICON)
            )
        }

        // Módulos stdlib (para autocompletar después de importar "...")
        val text = parameters.position.containingFile.text
        val offset = parameters.offset
        val prefix = text.substring(maxOf(0, offset - 30), offset)
        if (prefix.contains("importar")) {
            for (mod in stdlib_modules) {
                result.addElement(
                    LookupElementBuilder.create("\"$mod\"")
                        .withTypeText("stdlib")
                        .withIcon(CForgeFileType.ICON)
                )
            }
        }
    }
}
