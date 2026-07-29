package com.cforge.plugin.actions

import com.cforge.plugin.CForgeSettings
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.process.OSProcessHandler
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.execution.ui.RunContentManager
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent

// ── Acción: Abrir REPL interactivo ────────────────────────────────────────────
class OpenCForgeREPLAction : AnAction() {

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val settings = CForgeSettings.getInstance()

        // Intenta usar repl.py primero, luego --repl flag
        val replPath = settings.replPath
        val cmdLine = if (replPath.isNotBlank()) {
            GeneralCommandLine("python3", replPath)
        } else {
            GeneralCommandLine(settings.interpreterPath, "--repl")
        }.withWorkDirectory(project.basePath)
         .withRedirectErrorStream(true)

        val handler = OSProcessHandler(cmdLine)

        val consoleBuilder = TextConsoleBuilderFactory.getInstance()
            .createBuilder(project)
        val console = consoleBuilder.console
        console.attachToProcess(handler)

        val descriptor = RunContentDescriptor(
            console, handler,
            console.component,
            "C-Forge REPL"
        )

        RunContentManager.getInstance(project).showRunContent(
            com.intellij.execution.executors.DefaultRunExecutor.getRunExecutorInstance(),
            descriptor
        )
        handler.startNotify()
    }
}
