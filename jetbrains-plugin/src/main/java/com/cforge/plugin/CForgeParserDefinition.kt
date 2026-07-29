package com.cforge.plugin

import com.intellij.lang.ASTNode
import com.intellij.lang.ParserDefinition
import com.intellij.lang.PsiParser
import com.intellij.lexer.Lexer
import com.intellij.openapi.project.Project
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet

// ── PSI Simples ───────────────────────────────────────────────────────────────
class CForgePsiFile(viewProvider: FileViewProvider)
    : com.intellij.extapi.psi.PsiFileBase(viewProvider, CForgeLanguage) {
    override fun getFileType() = CForgeFileType.INSTANCE
    override fun toString()    = "CForge File"
}

class CForgePsiElement(node: ASTNode) : com.intellij.psi.impl.source.tree.LeafPsiElement(node.elementType, node.text)

// ── Parser trivial (stub) ─────────────────────────────────────────────────────
// Un parser completo requiere JFlex + Grammar-Kit. Esta implementación stub
// cumple el contrato requerido por el extension point lang.parserDefinition.
class CForgeParser : PsiParser {
    override fun parse(root: com.intellij.psi.tree.IElementType, builder: com.intellij.lang.PsiBuilder): ASTNode {
        val mark = builder.mark()
        while (!builder.eof()) builder.advanceLexer()
        mark.done(root)
        return builder.treeBuilt
    }
}

// ── ParserDefinition ──────────────────────────────────────────────────────────
class CForgeParserDefinition : ParserDefinition {
    companion object {
        val FILE = IFileElementType(CForgeLanguage)
    }

    override fun createLexer(project: Project?): Lexer = CForgeSimpleLexer()
    override fun createParser(project: Project?): PsiParser = CForgeParser()
    override fun getFileNodeType(): IFileElementType = FILE

    override fun getWhitespaceTokens(): TokenSet =
        TokenSet.create(CForgeTokenTypes.WHITESPACE)

    override fun getCommentTokens(): TokenSet =
        TokenSet.create(CForgeTokenTypes.COMMENT, CForgeTokenTypes.DOC_COMMENT)

    override fun getStringLiteralElements(): TokenSet =
        TokenSet.create(CForgeTokenTypes.STRING)

    override fun createElement(node: ASTNode): PsiElement = CForgePsiElement(node)

    override fun createFile(viewProvider: FileViewProvider): PsiFile = CForgePsiFile(viewProvider)
}
