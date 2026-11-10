// kompozc's language server in CLion: started for C++ sources with Compose code in them, which
// CLion's own C++ engine would read as broken C++. In those files, CLion's highlighting is turned
// off (the language server's diagnostics still show: they aren't a highlighting pass of CLion's),
// so what is wrong there comes from kompozc --lsp: kompozc's errors and clangd's, on their lines.
package dev.kompoz.clion

import com.intellij.codeInsight.daemon.impl.analysis.FileHighlightingSetting
import com.intellij.codeInsight.daemon.impl.analysis.HighlightLevelUtil
import com.intellij.codeInsight.daemon.impl.analysis.HighlightingSettingsPerFile
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiManager
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.intellij.platform.lsp.api.ProjectWideLspClientDescriptor
import java.util.concurrent.ConcurrentHashMap

/** Which files are C++ with Compose code: a line that starts a Kotlin declaration no C++ line starts. */
object ComposeSources {
    private val extensions = setOf("cpp", "cc", "cxx")
    private val kotlinDeclaration =
        Regex("""^(@Composable\b|@Serializable\b|(private |internal )?(fun|val|var) |object \w|data class )""", RegexOption.MULTILINE)
    private val known = ConcurrentHashMap<String, Pair<Long, Boolean>>()  // by path: the text's stamp, the answer

    fun isCompose(file: VirtualFile): Boolean {
        if (file.extension?.lowercase() !in extensions) return false
        val document = FileDocumentManager.getInstance().getCachedDocument(file)
        val stamp = document?.modificationStamp ?: file.modificationStamp
        known[file.path]?.let { (at, answer) -> if (at == stamp) return answer }
        val text = document?.text ?: runCatching { VfsUtilCore.loadText(file) }.getOrNull() ?: return false
        return kotlinDeclaration.containsMatchIn(text).also { known[file.path] = stamp to it }
    }
}

class KompozLspIntegrationProvider : LspIntegrationProvider {
    override fun fileOpened(project: Project, file: VirtualFile, clientStarter: LspIntegrationProvider.LspClientStarter) {
        if (!ComposeSources.isCompose(file)) return
        clientStarter.ensureClientStarted(KompozLspClientDescriptor(project))
        leaveToKompoz(project, file)
    }

    // CLion's own highlighting off in the file (its C++ engine would mark the Kotlin): the level
    // a user sets with the file's highlighting widget, as None.
    private fun leaveToKompoz(project: Project, file: VirtualFile) {
        ApplicationManager.getApplication().invokeLater {
            if (project.isDisposed) return@invokeLater
            val psi = PsiManager.getInstance(project).findFile(file) ?: return@invokeLater
            if (HighlightingSettingsPerFile.getInstance(project).getHighlightingSettingForRoot(psi) != FileHighlightingSetting.SKIP_HIGHLIGHTING)
                HighlightLevelUtil.forceRootHighlighting(psi, FileHighlightingSetting.SKIP_HIGHLIGHTING)
        }
    }
}

private class KompozLspClientDescriptor(project: Project) : ProjectWideLspClientDescriptor(project, "Kompoz") {
    override fun isSupportedFile(file: VirtualFile): Boolean = ComposeSources.isCompose(file)

    override fun createCommandLine(): GeneralCommandLine =
        GeneralCommandLine(KompozSettings.kompozc(project), "--lsp").withWorkDirectory(project.basePath)

    override fun getLanguageId(file: VirtualFile): String = "cpp"
}
