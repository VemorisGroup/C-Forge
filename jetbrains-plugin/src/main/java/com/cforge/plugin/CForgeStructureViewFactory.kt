package com.cforge.plugin

import com.intellij.ide.structureView.*
import com.intellij.ide.util.treeView.smartTree.TreeElement
import com.intellij.lang.PsiStructureViewFactory
import com.intellij.navigation.ItemPresentation
import com.intellij.openapi.editor.Editor
import com.intellij.psi.PsiFile
import javax.swing.Icon

// ── Elemento de estructura ────────────────────────────────────────────────────
data class StructureItem(
    val name: String,
    val kind: String,      // "funcion" | "clase" | "variable"
    val lineNum: Int
)

class CForgeStructureElement(
    private val item: StructureItem,
    private val children: List<CForgeStructureElement>
) : StructureViewTreeElement {

    override fun getValue() = item
    override fun navigate(requestFocus: Boolean) {}
    override fun canNavigate() = false
    override fun canNavigateToSource() = false

    override fun getPresentation() = object : ItemPresentation {
        override fun getPresentableText() = item.name
        override fun getLocationString()  = "línea ${item.lineNum}"
        override fun getIcon(unused: Boolean): Icon = CForgeFileType.ICON
    }

    override fun getChildren(): Array<TreeElement> = children.toTypedArray()
}

class CForgeStructureViewModel(
    psiFile: PsiFile,
    private val items: List<CForgeStructureElement>
) : StructureViewModelBase(psiFile, CForgeStructureElement(
    StructureItem(psiFile.name, "archivo", 0), emptyList()
)) {
    override fun getRoot(): StructureViewTreeElement = CForgeStructureElement(
        StructureItem(psiFile!!.name, "archivo", 0), items
    )
}

// ── Parser de estructura (extrae funciones y clases del texto) ────────────────
fun parseStructure(text: String): List<CForgeStructureElement> {
    val result = mutableListOf<CForgeStructureElement>()
    val lines = text.lines()
    val funcRe  = Regex("""^\s*funcion\s+(\w+)\s*\(""")
    val claseRe = Regex("""^\s*clase\s+(\w+)""")
    val seaRe   = Regex("""^\s*sea\s+(\w+)\s*=""")

    lines.forEachIndexed { idx, line ->
        funcRe.find(line)?.groupValues?.get(1)?.let { name ->
            result += CForgeStructureElement(StructureItem(name, "funcion", idx + 1), emptyList())
        }
        claseRe.find(line)?.groupValues?.get(1)?.let { name ->
            result += CForgeStructureElement(StructureItem(name, "clase", idx + 1), emptyList())
        }
        seaRe.find(line)?.groupValues?.get(1)?.let { name ->
            result += CForgeStructureElement(StructureItem(name, "variable", idx + 1), emptyList())
        }
    }
    return result
}

// ── Factory ───────────────────────────────────────────────────────────────────
class CForgeStructureViewFactory : PsiStructureViewFactory {
    override fun getStructureViewBuilder(psiFile: PsiFile): StructureViewBuilder =
        object : TreeBasedStructureViewBuilder() {
            override fun createStructureViewModel(editor: Editor?): StructureViewModel {
                val items = parseStructure(psiFile.text)
                return CForgeStructureViewModel(psiFile, items)
            }
        }
}
