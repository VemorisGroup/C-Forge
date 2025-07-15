package com.cforge.plugin.actions

import com.cforge.plugin.CForgeFileType
import com.cforge.plugin.CForgeSettings
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.process.OSProcessHandler
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.execution.ui.RunContentManager
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.project.Project

// ── Acción: Ejecutar archivo .cfv actual ──────────────────────────────────────
class RunCForgeFileAction : AnAction() {

    override fun update(e: AnActionEvent) {
        val file = e.getData(CommonDataKeys.VIRTUAL_FILE)
        e.presentation.isEnabledAndVisible = file?.extension == "cfv"
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val file    = e.getData(CommonDataKeys.VIRTUAL_FILE) ?: return
        val settings = CForgeSettings.getInstance()

        runFile(project, file.path, settings.interpreterPath)
    }

    companion object {
        fun runFile(project: Project, filePath: String, interpreterPath: String) {
            val cmdLine = GeneralCommandLine(interpreterPath, filePath)
                .withWorkDirectory(project.basePath)
                .withRedirectErrorStream(true)

            val handler = OSProcessHandler(cmdLine)

            val consoleBuilder = TextConsoleBuilderFactory.getInstance()
                .createBuilder(project)
            val console = consoleBuilder.console
            console.attachToProcess(handler)

            val descriptor = RunContentDescriptor(
                console, handler,
                console.component,
                "C-Forge: ${filePath.substringAfterLast('/')}"
            )
            RunContentManager.getInstance(project).showRunContent(
                com.intellij.execution.executors.DefaultRunExecutor.getRunExecutorInstance(),
                descriptor
            )
            handler.startNotify()
        }
    }
}
