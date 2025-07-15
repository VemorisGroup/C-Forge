package com.cforge.plugin.actions

import com.cforge.plugin.CForgeSettings
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.process.OSProcessHandler
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.execution.ui.RunContentManager
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent

// ── Acción: Ejecutar tests con cftest ─────────────────────────────────────────
class RunCForgeTestsAction : AnAction() {

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val settings = CForgeSettings.getInstance()

        val cftestPath = settings.cftestPath.ifBlank { "cftest" }
        val projectBase = project.basePath ?: return

        // Busca tests/ o el archivo actual
        val testDir = "$projectBase/tests"

        val cmdLine = GeneralCommandLine("python3", cftestPath, testDir)
            .withWorkDirectory(projectBase)
            .withRedirectErrorStream(true)

        val handler = OSProcessHandler(cmdLine)

        val consoleBuilder = TextConsoleBuilderFactory.getInstance()
            .createBuilder(project)
        val console = consoleBuilder.console
        console.attachToProcess(handler)

        val descriptor = RunContentDescriptor(
            console, handler,
            console.component,
            "C-Forge Tests"
        )

        RunContentManager.getInstance(project).showRunContent(
            com.intellij.execution.executors.DefaultRunExecutor.getRunExecutorInstance(),
            descriptor
        )
        handler.startNotify()
    }
}
