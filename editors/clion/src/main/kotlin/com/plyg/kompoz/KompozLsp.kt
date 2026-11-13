// kompozc's language server in CLion: started for C++ sources with Kotlin code in them, which
// CLion's own C++ engine would read as broken C++. In those files, CLion's highlighting is turned
// off (the language server's diagnostics still show: they aren't a highlighting pass of CLion's),
// so what is wrong there comes from kompozc --lsp: kompozc's errors and clangd's, on their lines.
package com.plyg.kompoz

import com.intellij.codeInsight.daemon.impl.analysis.FileHighlightingSetting
import com.intellij.codeInsight.daemon.impl.analysis.FileHighlightingSettingListener
import com.intellij.codeInsight.daemon.impl.analysis.HighlightLevelUtil
import com.intellij.codeInsight.daemon.impl.analysis.HighlightingSettingsPerFile
import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.application.ModalityState
import com.intellij.openapi.application.runReadActionBlocking
import com.intellij.openapi.diagnostic.logger
import com.intellij.openapi.fileEditor.FileEditorManager
import com.intellij.openapi.fileEditor.FileEditorManagerListener
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiManager
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.intellij.platform.lsp.api.ProjectWideLspClientDescriptor
import java.util.concurrent.ConcurrentHashMap

private val LOG = logger<KompozLspIntegrationProvider>()

/** Which files are C++ with Kotlin code: a line that starts a Kotlin declaration no C++ line starts. */
object KompozableSources {
    private val extensions = setOf("cpp", "cc", "cxx")
    private val kotlinDeclaration =
        Regex("""^(@Kompozable\b|@Serializable\b|(private |internal )?(fun|val|var) |object \w|data class )""", RegexOption.MULTILINE)
    private val known = ConcurrentHashMap<String, Pair<Long, Boolean>>()  // by path: the text's stamp, the answer

    fun isKompozable(file: VirtualFile): Boolean {
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
        if (!KompozableSources.isKompozable(file)) return
        LOG.info("Kompoz: ${file.path} has Kotlin code: kompozc --lsp for it")
        clientStarter.ensureClientStarted(KompozLspClientDescriptor(project))
        KompozableHighlighting.leaveToKompoz(project, file)
    }
}

/** A file with Kotlin code opened in an editor: CLion's highlighting off in it (again, if it was put back). */
class KompozEditorListener(private val project: Project) : FileEditorManagerListener {
    override fun fileOpened(source: FileEditorManager, file: VirtualFile) {
        if (KompozableSources.isKompozable(file)) KompozableHighlighting.leaveToKompoz(project, file)
    }
}

/**
 * A file with Kotlin code given another highlighting level: off again. CLion's C++ engine (Nova) keeps a level of its
 * own for each file and, when it takes a file up, puts that one on the file (its default: all problems), after the
 * plugin set it; a change of the file's level is what Nova sends back to its engine, so setting it off again here
 * reaches the engine too.
 */
class KompozHighlightingListener(private val project: Project) : FileHighlightingSettingListener {
    override fun settingChanged(root: PsiElement, setting: FileHighlightingSetting) {
        if (setting == FileHighlightingSetting.SKIP_HIGHLIGHTING) return
        val file = root.containingFile?.virtualFile ?: return
        if (!KompozableSources.isKompozable(file)) return
        LOG.info("Kompoz: ${file.path}: highlighting set to $setting: off again")
        KompozableHighlighting.leaveToKompoz(project, file)
    }
}

// CLion's own highlighting off in a file (its C++ engine would mark the Kotlin): the level a user sets
// with the file's highlighting widget, as None.
object KompozableHighlighting {
    fun leaveToKompoz(project: Project, file: VirtualFile) {
        // (non-modal: setting the level changes PSI, which needs a write-safe moment; one installed from the settings
        // dialog with the file open does it as the dialog closes)
        ApplicationManager.getApplication().invokeLater({
            if (project.isDisposed) return@invokeLater
            val psi = runReadActionBlocking { PsiManager.getInstance(project).findFile(file) }
            if (psi == null) {
                LOG.warn("Kompoz: no PSI for ${file.path}: its highlighting is left as it is")
                return@invokeLater
            }
            val settings = HighlightingSettingsPerFile.getInstance(project)
            val before = settings.getHighlightingSettingForRoot(psi)
            if (before != FileHighlightingSetting.SKIP_HIGHLIGHTING) HighlightLevelUtil.forceRootHighlighting(psi, FileHighlightingSetting.SKIP_HIGHLIGHTING)
            LOG.info("Kompoz: ${file.path}: highlighting $before -> ${settings.getHighlightingSettingForRoot(psi)} (${psi.language.id})")
        }, ModalityState.nonModal())
    }
}

private class KompozLspClientDescriptor(project: Project) : ProjectWideLspClientDescriptor(project, "Kompoz") {
    override fun isSupportedFile(file: VirtualFile): Boolean = KompozableSources.isKompozable(file)

    override fun createCommandLine(): GeneralCommandLine =
        GeneralCommandLine(KompozSettings.kompozc(project), "--lsp").withWorkDirectory(project.basePath)

    override fun getLanguageId(file: VirtualFile): String = "cpp"
}
