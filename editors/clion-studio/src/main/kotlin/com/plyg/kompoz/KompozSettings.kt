// Where kompozc is: set in Settings, Tools, Kompoz; else KOMPOZC from the environment; else one
// built in the project (build/compiler/kompozc, cmake-build-*/compiler/kompozc); else kompozc on PATH.
package com.plyg.kompoz

import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage
import com.intellij.openapi.components.service
import com.intellij.openapi.options.BoundConfigurable
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.SystemInfo
import com.intellij.ui.dsl.builder.bindText
import com.intellij.ui.dsl.builder.columns
import com.intellij.ui.dsl.builder.COLUMNS_LARGE
import com.intellij.ui.dsl.builder.panel
import java.io.File

@Service(Service.Level.APP)
@State(name = "KompozSettings", storages = [Storage("kompoz.xml")])
class KompozSettings : PersistentStateComponent<KompozSettings.Values> {
    class Values {
        var kompozcPath: String = ""
    }

    private var values = Values()

    override fun getState(): Values = values

    override fun loadState(state: Values) {
        values = state
    }

    var kompozcPath: String
        get() = values.kompozcPath
        set(value) {
            values.kompozcPath = value.trim()
        }

    companion object {
        fun kompozc(project: Project): String {
            service<KompozSettings>().kompozcPath.takeIf { it.isNotEmpty() }?.let { return it }
            System.getenv("KOMPOZC")?.takeIf { it.isNotEmpty() }?.let { return it }
            val name = if (SystemInfo.isWindows) "kompozc.exe" else "kompozc"
            val base = project.basePath?.let(::File)
            val built = base?.listFiles()?.filter { it.isDirectory && (it.name == "build" || it.name.startsWith("cmake-build-")) }
                ?.map { File(it, "compiler/$name") }
                ?.filter { it.canExecute() }
                ?.maxByOrNull { it.lastModified() }
            return built?.path ?: name
        }
    }
}

class KompozConfigurable : BoundConfigurable("Kompoz") {
    override fun createPanel() = panel {
        row("kompozc:") {
            textField()
                .bindText(service<KompozSettings>()::kompozcPath)
                .columns(COLUMNS_LARGE)
                .comment("Empty: KOMPOZC from the environment, else build/ or cmake-build-*/compiler/kompozc in the project, else kompozc on PATH.")
        }
    }
}
