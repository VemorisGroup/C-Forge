package com.cforge.plugin

import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.Annotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile

// ── Anotador (linting en tiempo real via cflint) ──────────────────────────────
class CForgeAnnotator : Annotator {

    override fun annotate(element: PsiElement, holder: AnnotationHolder) {
        if (element !is PsiFile) return
        if (element.fileType != CForgeFileType.INSTANCE) return

        val settings = CForgeSettings.getInstance()
        val cflintPath = settings.cflintPath.ifBlank { null } ?: return

        try {
            runCflint(element, cflintPath, holder)
        } catch (_: Exception) {
            // Silently ignore — cflint may not be installed
        }
    }

    private fun runCflint(file: PsiFile, cflintPath: String, holder: AnnotationHolder) {
        val filePath = file.virtualFile?.path ?: return
        val proc = ProcessBuilder("python3", cflintPath, filePath, "--json")
            .redirectErrorStream(true)
            .start()

        val output = proc.inputStream.bufferedReader().readText()
        proc.waitFor()

        if (output.isBlank()) return

        // Parse JSON output: [{"file":"...","line":N,"col":N,"severity":"error|warning","code":"CF001","message":"..."}]
        parseJsonDiags(output).forEach { diag ->
            val lineStart = getLineOffset(file.text, diag.line - 1)
            if (lineStart < 0 || lineStart >= file.textLength) return@forEach
            val lineEnd = file.text.indexOf('\n', lineStart).takeIf { it > 0 } ?: file.textLength
            val range = TextRange(lineStart, minOf(lineEnd, file.textLength))

            val severity = if (diag.severity == "error") HighlightSeverity.ERROR else HighlightSeverity.WARNING
            holder.newAnnotation(severity, "[${diag.code}] ${diag.message}")
                .range(range)
                .create()
        }
    }

    private data class Diag(val line: Int, val severity: String, val code: String, val message: String)

    private fun parseJsonDiags(json: String): List<Diag> {
        val result = mutableListOf<Diag>()
        // Minimal JSON parser — avoids adding a library dependency
        val itemRe = Regex("""\{[^}]+\}""")
        val lineRe = Regex(""""line"\s*:\s*(\d+)""")
        val sevRe  = Regex(""""severity"\s*:\s*"([^"]+)"""")
        val codeRe = Regex(""""code"\s*:\s*"([^"]+)"""")
        val msgRe  = Regex(""""message"\s*:\s*"([^"]+)"""")

        for (match in itemRe.findAll(json)) {
            val obj = match.value
            val line = lineRe.find(obj)?.groupValues?.get(1)?.toIntOrNull() ?: continue
            val sev  = sevRe.find(obj)?.groupValues?.get(1) ?: "warning"
            val code = codeRe.find(obj)?.groupValues?.get(1) ?: ""
            val msg  = msgRe.find(obj)?.groupValues?.get(1) ?: ""
            result += Diag(line, sev, code, msg)
        }
        return result
    }

    private fun getLineOffset(text: String, lineIndex: Int): Int {
        var line = 0
        var i = 0
        while (i < text.length) {
            if (line == lineIndex) return i
            if (text[i] == '\n') line++
            i++
        }
        return -1
    }
}
