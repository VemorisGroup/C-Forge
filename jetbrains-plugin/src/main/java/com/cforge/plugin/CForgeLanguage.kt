package com.cforge.plugin

import com.intellij.lang.Language
import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.openapi.util.IconLoader
import javax.swing.Icon

// ── Lenguaje ──────────────────────────────────────────────────────────────────
object CForgeLanguage : Language("CForge") {
    override fun getDisplayName() = "C-Forge"
}

// ── Tipo de archivo ────────────────────────────────────────────────────────────
class CForgeFileType private constructor() : LanguageFileType(CForgeLanguage) {
    companion object {
        @JvmField val INSTANCE = CForgeFileType()
        val ICON: Icon by lazy { IconLoader.getIcon("/icons/cforge.svg", CForgeFileType::class.java) }
    }
    override fun getName()        = "C-Forge"
    override fun getDescription() = "Archivo de código C-Forge"
    override fun getDefaultExtension() = "cfv"
    override fun getIcon()        = ICON
}
