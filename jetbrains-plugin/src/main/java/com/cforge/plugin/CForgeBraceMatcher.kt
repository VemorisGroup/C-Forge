package com.cforge.plugin

import com.intellij.lang.BracePair
import com.intellij.lang.PairedBraceMatcher
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IElementType

// ── Matching de llaves/corchetes/paréntesis ───────────────────────────────────
class CForgeBraceMatcher : PairedBraceMatcher {
    private val pairs = arrayOf(
        BracePair(CForgeTokenTypes.BRACE,   CForgeTokenTypes.BRACE,   true),
        BracePair(CForgeTokenTypes.BRACKET, CForgeTokenTypes.BRACKET, false),
        BracePair(CForgeTokenTypes.PAREN,   CForgeTokenTypes.PAREN,   false)
    )

    override fun getPairs() = pairs
    override fun isPairedBracesAllowedBeforeType(lbraceType: IElementType, contextType: IElementType?) = true
    override fun getCodeConstructStart(file: PsiFile?, openingBraceOffset: Int) = openingBraceOffset
}
