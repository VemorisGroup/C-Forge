package com.cforge.plugin.actions

import com.cforge.plugin.CForgeSettings
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.execution.filters.TextConsoleBuilderFactory
import com.intellij.execution.process.OSProcessHandler
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.execution.ui.RunContentManager
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import java.io.File

// ── Acción: Build del proyecto con cfbuild ────────────────────────────────────
class BuildCForgeProjectAction : AnAction() {

    override fun update(e: AnActionEvent) {
        val project = e.project
        val hasBuildFile = project?.basePath?.let {
            File("$it/cforge.toml").exists()
        } ?: false
        e.presentation.isEnabled = hasBuildFile
    }

    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val settings = CForgeSettings.getInstance()

        val cfbuildPath = settings.cfbuildPath.ifBlank { "cfbuild" }
        val projectBase = project.basePath ?: return

        val cmdLine = GeneralCommandLine("python3", cfbuildPath, "build")
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
            "C-Forge Build"
        )

        RunContentManager.getInstance(project).showRunContent(
            com.intellij.execution.executors.DefaultRunExecutor.getRunExecutorInstance(),
            descriptor
        )
        handler.startNotify()
    }
}
