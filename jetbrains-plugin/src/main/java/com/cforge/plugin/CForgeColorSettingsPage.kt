package com.cforge.plugin

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.options.colors.*
import javax.swing.Icon

// ── Página de configuración de colores ────────────────────────────────────────
class CForgeColorSettingsPage : ColorSettingsPage {
    override fun getIcon(): Icon = CForgeFileType.ICON
    override fun getHighlighter(): SyntaxHighlighter = CForgeSyntaxHighlighter()
    override fun getDisplayName() = "C-Forge"

    override fun getDemoText() = """
        // Comentario de línea
        /// @doc Comentario de documentación
        importar "matematica"
        importar "lista"

        clase Persona {
            funcion constructor(nombre: texto, edad: numero) {
                esto.nombre = nombre
                esto.edad   = edad
            }
            funcion saludar(): texto {
                retornar "Hola, soy " + esto.nombre
            }
        }

        sea p = nuevo Persona("Ana", 30)
        sea x: numero = 42
        sea activo: booleano = verdadero
        sea vacio = nulo

        si (activo y x > 0) {
            mostrar(p.saludar())
        } sino {
            mostrar("inactivo")
        }

        para item en rango(1, 10) {
            mostrar(item)
        }
    """.trimIndent()

    override fun getAdditionalHighlightingTagToDescriptorMap(): Map<String, TextAttributesKey>? = null

    override fun getAttributeDescriptors() = arrayOf(
        AttributesDescriptor("Palabras clave",       CForgeColors.KEYWORD),
        AttributesDescriptor("Cadenas de texto",     CForgeColors.STRING),
        AttributesDescriptor("Números",              CForgeColors.NUMBER),
        AttributesDescriptor("Comentarios",          CForgeColors.COMMENT),
        AttributesDescriptor("Documentación (///)",  CForgeColors.DOC_COMMENT),
        AttributesDescriptor("Funciones builtin",    CForgeColors.BUILTIN),
        AttributesDescriptor("Nombres de función",   CForgeColors.FUNCTION),
        AttributesDescriptor("Variables",            CForgeColors.VARIABLE),
        AttributesDescriptor("Operadores",           CForgeColors.OPERATOR),
        AttributesDescriptor("Llaves {}",            CForgeColors.BRACE),
        AttributesDescriptor("Corchetes []",         CForgeColors.BRACKET),
        AttributesDescriptor("Paréntesis ()",        CForgeColors.PAREN),
        AttributesDescriptor("Tipos",                CForgeColors.TYPE),
        AttributesDescriptor("Booleanos",            CForgeColors.BOOLEAN),
        AttributesDescriptor("Nulo",                 CForgeColors.NULL)
    )

    override fun getColorDescriptors(): Array<ColorDescriptor> = ColorDescriptor.EMPTY_ARRAY
}
