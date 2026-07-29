package com.cforge.plugin

import com.intellij.lang.CodeDocumentationAwareCommenter
import com.intellij.psi.PsiComment
import com.intellij.psi.tree.IElementType

// ── Comentarios (Ctrl+/) ──────────────────────────────────────────────────────
class CForgeCommenter : CodeDocumentationAwareCommenter {
    override fun getLineCommentPrefix()             = "//"
    override fun getBlockCommentPrefix()            = "/*"
    override fun getBlockCommentSuffix()            = "*/"
    override fun getCommentedBlockCommentPrefix()   = null
    override fun getCommentedBlockCommentSuffix()   = null
    override fun getLineCommentTokenType(): IElementType = CForgeTokenTypes.COMMENT
    override fun getBlockCommentTokenType(): IElementType = CForgeTokenTypes.COMMENT
    override fun getDocumentationCommentTokenType(): IElementType = CForgeTokenTypes.DOC_COMMENT
    override fun getDocumentationCommentPrefix()    = "///"
    override fun getDocumentationCommentLinePrefix() = "///"
    override fun getDocumentationCommentSuffix()    = null
    override fun isDocumentationComment(element: PsiComment?) =
        element?.text?.startsWith("///") == true
}
