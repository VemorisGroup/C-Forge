package com.cforge.plugin

import com.intellij.lang.ASTNode
import com.intellij.lang.folding.FoldingBuilderEx
import com.intellij.lang.folding.FoldingDescriptor
import com.intellij.openapi.editor.Document
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile

// ── Folding de código (colapsar funciones/clases) ─────────────────────────────
class CForgeFoldingBuilder : FoldingBuilderEx() {

    override fun buildFoldRegions(root: PsiElement, document: Document, quick: Boolean): Array<FoldingDescriptor> {
        if (root !is PsiFile) return emptyArray()

        val text = root.text
        val descriptors = mutableListOf<FoldingDescriptor>()
        val node = root.node ?: return emptyArray()

        // Encuentra bloques { ... } que van por múltiples líneas
        var depth = 0
        val stack = ArrayDeque<Int>() // posiciones de llaves de apertura

        for (i in text.indices) {
            when (text[i]) {
                '{' -> {
                    stack.addLast(i)
                    depth++
                }
                '}' -> {
                    if (stack.isNotEmpty()) {
                        val start = stack.removeLast()
                        depth--
                        val end = i + 1
                        // Solo crear fold si el bloque ocupa más de 1 línea
                        val startLine = document.getLineNumber(start)
                        val endLine   = document.getLineNumber(minOf(end - 1, text.length - 1))
                        if (endLine > startLine) {
                            val range = TextRange(start, minOf(end, text.length))
                            descriptors.add(FoldingDescriptor(node, range))
                        }
                    }
                }
            }
        }
        return descriptors.toTypedArray()
    }

    override fun getPlaceholderText(node: ASTNode): String = "{ ... }"

    override fun isCollapsedByDefault(node: ASTNode) = false
}
