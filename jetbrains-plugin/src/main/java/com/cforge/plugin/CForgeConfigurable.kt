package com.cforge.plugin

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.*
import com.intellij.openapi.options.Configurable
import com.intellij.openapi.ui.TextFieldWithBrowseButton
import com.intellij.ui.components.JBLabel
import com.intellij.ui.components.JBTextField
import com.intellij.util.ui.FormBuilder
import javax.swing.JComponent
import javax.swing.JPanel

// ── Ajustes persistentes ──────────────────────────────────────────────────────
@State(name = "CForgeSettings", storages = [Storage("CForgeSettings.xml")])
@Service(Service.Level.APP)
class CForgeSettings : PersistentStateComponent<CForgeSettings.State> {
    data class State(
        var interpreterPath: String = "cforgev",
        var cflintPath: String = "",
        var cftestPath: String = "",
        var cfbuildPath: String = "",
        var cfmtPath: String = "",
        var replPath: String = ""
    )

    private var state = State()

    override fun getState() = state
    override fun loadState(state: State) { this.state = state }

    var interpreterPath: String
        get() = state.interpreterPath
        set(v) { state.interpreterPath = v }

    var cflintPath: String
        get() = state.cflintPath
        set(v) { state.cflintPath = v }

    var cftestPath: String
        get() = state.cftestPath
        set(v) { state.cftestPath = v }

    var cfbuildPath: String
        get() = state.cfbuildPath
        set(v) { state.cfbuildPath = v }

    var cfmtPath: String
        get() = state.cfmtPath
        set(v) { state.cfmtPath = v }

    var replPath: String
        get() = state.replPath
        set(v) { state.replPath = v }

    companion object {
        fun getInstance(): CForgeSettings =
            ApplicationManager.getApplication().getService(CForgeSettings::class.java)
    }
}

// ── Panel de UI de configuración ──────────────────────────────────────────────
class CForgeSettingsPanel {
    val interpreterPathField = JBTextField()
    val cflintPathField      = JBTextField()
    val cftestPathField      = JBTextField()
    val cfbuildPathField     = JBTextField()
    val cfmtPathField        = JBTextField()
    val replPathField        = JBTextField()

    val panel: JPanel = FormBuilder.createFormBuilder()
        .addLabeledComponent(JBLabel("Intérprete cforgev:"), interpreterPathField, 1, false)
        .addLabeledComponent(JBLabel("Ruta cflint:"),        cflintPathField,      1, false)
        .addLabeledComponent(JBLabel("Ruta cftest:"),        cftestPathField,      1, false)
        .addLabeledComponent(JBLabel("Ruta cfbuild:"),       cfbuildPathField,     1, false)
        .addLabeledComponent(JBLabel("Ruta cfmt:"),          cfmtPathField,        1, false)
        .addLabeledComponent(JBLabel("Ruta REPL:"),          replPathField,        1, false)
        .addComponentFillVertically(JPanel(), 0)
        .panel
}

// ── Configurable (aparece en Settings > Tools > C-Forge) ─────────────────────
class CForgeConfigurable : Configurable {
    private var panel: CForgeSettingsPanel? = null

    override fun getDisplayName() = "C-Forge"

    override fun createComponent(): JComponent {
        panel = CForgeSettingsPanel()
        return panel!!.panel
    }

    override fun isModified(): Boolean {
        val s = CForgeSettings.getInstance()
        val p = panel ?: return false
        return p.interpreterPathField.text != s.interpreterPath ||
               p.cflintPathField.text      != s.cflintPath      ||
               p.cftestPathField.text      != s.cftestPath      ||
               p.cfbuildPathField.text     != s.cfbuildPath     ||
               p.cfmtPathField.text        != s.cfmtPath        ||
               p.replPathField.text        != s.replPath
    }

    override fun apply() {
        val s = CForgeSettings.getInstance()
        val p = panel ?: return
        s.interpreterPath = p.interpreterPathField.text
        s.cflintPath      = p.cflintPathField.text
        s.cftestPath      = p.cftestPathField.text
        s.cfbuildPath     = p.cfbuildPathField.text
        s.cfmtPath        = p.cfmtPathField.text
        s.replPath        = p.replPathField.text
    }

    override fun reset() {
        val s = CForgeSettings.getInstance()
        val p = panel ?: return
        p.interpreterPathField.text = s.interpreterPath
        p.cflintPathField.text      = s.cflintPath
        p.cftestPathField.text      = s.cftestPath
        p.cfbuildPathField.text     = s.cfbuildPath
        p.cfmtPathField.text        = s.cfmtPath
        p.replPathField.text        = s.replPath
    }

    override fun disposeUIResources() { panel = null }
}
