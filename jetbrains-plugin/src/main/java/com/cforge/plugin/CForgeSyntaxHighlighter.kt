package com.cforge.plugin

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as C
import com.intellij.openapi.editor.HighlighterColors
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.editor.colors.TextAttributesKey.createTextAttributesKey as key
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.tree.IElementType

// ── Colores ───────────────────────────────────────────────────────────────────
object CForgeColors {
    val KEYWORD     = key("CFORGE_KEYWORD",     C.KEYWORD)
    val STRING      = key("CFORGE_STRING",      C.STRING)
    val NUMBER      = key("CFORGE_NUMBER",      C.NUMBER)
    val COMMENT     = key("CFORGE_COMMENT",     C.LINE_COMMENT)
    val BUILTIN     = key("CFORGE_BUILTIN",     C.PREDEFINED_SYMBOL)
    val FUNCTION    = key("CFORGE_FUNCTION",    C.FUNCTION_CALL)
    val VARIABLE    = key("CFORGE_VARIABLE",    C.LOCAL_VARIABLE)
    val OPERATOR    = key("CFORGE_OPERATOR",    C.OPERATION_SIGN)
    val BRACE       = key("CFORGE_BRACE",       C.BRACES)
    val BRACKET     = key("CFORGE_BRACKET",     C.BRACKETS)
    val PAREN       = key("CFORGE_PAREN",       C.PARENTHESES)
    val DOC_COMMENT = key("CFORGE_DOC_COMMENT", C.DOC_COMMENT)
    val TYPE        = key("CFORGE_TYPE",        C.CLASS_NAME)
    val BOOLEAN     = key("CFORGE_BOOLEAN",     C.PREDEFINED_SYMBOL)
    val NULL        = key("CFORGE_NULL",        C.PREDEFINED_SYMBOL)
}

// ── Lexer simple (regex-based) ────────────────────────────────────────────────
val KEYWORDS = setOf(
    "sea", "funcion", "retornar", "si", "sino", "esi", "para", "en",
    "mientras", "segun", "caso", "romper", "continuar", "clase", "nuevo",
    "importar", "exportar", "lanzar", "intentar", "capturar", "finalmente",
    "rango", "y", "o", "no", "de", "extiende", "super", "esto"
)

val BUILTINS = setOf(
    "mostrar", "tipo_de", "longitud", "agregar", "eliminar", "insertar",
    "ordenar", "invertir", "filtrar", "mapear", "reducir",
    "texto_a_numero", "numero_a_texto", "texto_dividir", "texto_unir",
    "texto_reemplazar", "texto_contiene", "texto_mayusculas", "texto_minusculas",
    "texto_trim", "json_parsear", "json_texto", "piso", "techo", "redondear",
    "absoluto", "potencia", "raiz", "maximo", "minimo", "aleatorio",
    "tiempo_ms", "timestamp", "dormir", "env_obtener", "leer_archivo",
    "escribir_archivo", "existe_archivo", "http_get", "http_post",
    "mapa_claves", "mapa_valores", "mapa_entradas", "mapa_fusionar",
    "tiene_clave", "lista_suma", "texto_formato", "numero_formato",
    "db_query", "db_exec", "pg_query", "mysql_query",
    "hilo_crear", "mutex_crear", "canal_crear", "canal_enviar", "canal_recibir"
)

val TYPES = setOf(
    "texto", "numero", "booleano", "lista", "mapa", "nulo", "cualquiera",
    "entero", "decimal", "tupla", "conjunto"
)

val BOOLEANS = setOf("verdadero", "falso")

class CForgeSimpleLexer : Lexer() {
    // Implementación simplificada — en producción usaría JFlex
    // Esta versión delega al resaltado por token patterns
    private var buffer: CharSequence = ""
    private var start = 0; private var end = 0; private var pos = 0

    override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
        this.buffer = buffer; this.start = startOffset; this.end = endOffset; this.pos = startOffset
        advance()
    }
    override fun getState() = 0
    override fun getTokenType(): IElementType? = currentToken
    override fun getTokenStart() = tokenStart
    override fun getTokenEnd() = pos
    override fun getBufferSequence() = buffer
    override fun getBufferEnd() = end

    private var currentToken: IElementType? = null
    private var tokenStart = 0

    override fun advance() {
        if (pos >= end) { currentToken = null; return }
        tokenStart = pos
        val c = buffer[pos]

        // Comentario doc
        if (c == '/' && pos + 2 < end && buffer[pos+1] == '/' && buffer[pos+2] == '/') {
            while (pos < end && buffer[pos] != '\n') pos++
            currentToken = CForgeTokenTypes.DOC_COMMENT; return
        }
        // Comentario normal
        if (c == '/' && pos + 1 < end && buffer[pos+1] == '/') {
            while (pos < end && buffer[pos] != '\n') pos++
            currentToken = CForgeTokenTypes.COMMENT; return
        }
        // String
        if (c == '"' || c == '\'') {
            val quote = c; pos++
            while (pos < end && buffer[pos] != quote) {
                if (buffer[pos] == '\\') pos++
                pos++
            }
            if (pos < end) pos++
            currentToken = CForgeTokenTypes.STRING; return
        }
        // Número
        if (c.isDigit() || (c == '-' && pos + 1 < end && buffer[pos+1].isDigit())) {
            if (c == '-') pos++
            while (pos < end && (buffer[pos].isDigit() || buffer[pos] == '.')) pos++
            currentToken = CForgeTokenTypes.NUMBER; return
        }
        // Identificador / palabra clave
        if (c.isLetter() || c == '_') {
            while (pos < end && (buffer[pos].isLetterOrDigit() || buffer[pos] == '_')) pos++
            val word = buffer.subSequence(tokenStart, pos).toString()
            currentToken = when {
                word in KEYWORDS  -> CForgeTokenTypes.KEYWORD
                word in BUILTINS  -> CForgeTokenTypes.BUILTIN
                word in TYPES     -> CForgeTokenTypes.TYPE
                word in BOOLEANS  -> CForgeTokenTypes.BOOLEAN
                word == "nulo"    -> CForgeTokenTypes.NULL
                else              -> CForgeTokenTypes.IDENTIFIER
            }
            return
        }
        // Operadores y puntuación
        pos++
        currentToken = when (c) {
            '{', '}' -> CForgeTokenTypes.BRACE
            '[', ']' -> CForgeTokenTypes.BRACKET
            '(', ')' -> CForgeTokenTypes.PAREN
            '+','-','*','/','%','=','<','>','!','&','|','?','.' -> CForgeTokenTypes.OPERATOR
            '\n', '\r', ' ', '\t' -> CForgeTokenTypes.WHITESPACE
            else -> CForgeTokenTypes.OTHER
        }
    }
}

object CForgeTokenTypes {
    val KEYWORD     = IElementType("KEYWORD",     CForgeLanguage)
    val BUILTIN     = IElementType("BUILTIN",     CForgeLanguage)
    val STRING      = IElementType("STRING",      CForgeLanguage)
    val NUMBER      = IElementType("NUMBER",      CForgeLanguage)
    val COMMENT     = IElementType("COMMENT",     CForgeLanguage)
    val DOC_COMMENT = IElementType("DOC_COMMENT", CForgeLanguage)
    val IDENTIFIER  = IElementType("IDENTIFIER",  CForgeLanguage)
    val OPERATOR    = IElementType("OPERATOR",    CForgeLanguage)
    val BRACE       = IElementType("BRACE",       CForgeLanguage)
    val BRACKET     = IElementType("BRACKET",     CForgeLanguage)
    val PAREN       = IElementType("PAREN",       CForgeLanguage)
    val TYPE        = IElementType("TYPE",        CForgeLanguage)
    val BOOLEAN     = IElementType("BOOLEAN",     CForgeLanguage)
    val NULL        = IElementType("NULL",        CForgeLanguage)
    val WHITESPACE  = IElementType("WHITESPACE",  CForgeLanguage)
    val OTHER       = IElementType("OTHER",       CForgeLanguage)
}

// ── Syntax Highlighter ────────────────────────────────────────────────────────
class CForgeSyntaxHighlighter : SyntaxHighlighterBase() {
    override fun getHighlightingLexer() = CForgeSimpleLexer()

    override fun getTokenHighlights(tokenType: IElementType): Array<TextAttributesKey> =
        when (tokenType) {
            CForgeTokenTypes.KEYWORD     -> arrayOf(CForgeColors.KEYWORD)
            CForgeTokenTypes.BUILTIN     -> arrayOf(CForgeColors.BUILTIN)
            CForgeTokenTypes.STRING      -> arrayOf(CForgeColors.STRING)
            CForgeTokenTypes.NUMBER      -> arrayOf(CForgeColors.NUMBER)
            CForgeTokenTypes.COMMENT     -> arrayOf(CForgeColors.COMMENT)
            CForgeTokenTypes.DOC_COMMENT -> arrayOf(CForgeColors.DOC_COMMENT)
            CForgeTokenTypes.OPERATOR    -> arrayOf(CForgeColors.OPERATOR)
            CForgeTokenTypes.BRACE       -> arrayOf(CForgeColors.BRACE)
            CForgeTokenTypes.BRACKET     -> arrayOf(CForgeColors.BRACKET)
            CForgeTokenTypes.PAREN       -> arrayOf(CForgeColors.PAREN)
            CForgeTokenTypes.TYPE        -> arrayOf(CForgeColors.TYPE)
            CForgeTokenTypes.BOOLEAN     -> arrayOf(CForgeColors.BOOLEAN)
            CForgeTokenTypes.NULL        -> arrayOf(CForgeColors.NULL)
            else -> emptyArray()
        }
}

class CForgeSyntaxHighlighterFactory : SyntaxHighlighterFactory() {
    override fun getSyntaxHighlighter(project: Project?, virtualFile: VirtualFile?) =
        CForgeSyntaxHighlighter()
}
