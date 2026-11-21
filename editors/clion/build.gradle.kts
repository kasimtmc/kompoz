// Kompoz's CLion plugin. Built against a CLion of yours (-PclionPath=/path/to/clion: no download) or
// against CLion 2026.3 downloaded:
//
//   JAVA_HOME=/path/to/clion/jbr ./gradlew buildPlugin -PclionPath=/path/to/clion
//
// (Gradle needs a JDK: CLion's own, its jbr/, will do.)
// build/distributions/kompoz-clion-*.zip is then installed in CLion from disk (Settings, Plugins). A tag clion-v<version>
// releases it, on GitHub and JetBrains Marketplace (.github/workflows/clion-release.yml).

plugins {
    id("org.jetbrains.kotlin.jvm") version "2.4.21"
    id("org.jetbrains.intellij.platform") version "2.20.1"
}

group = "com.plyg"
version = "0.2.2"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    intellijPlatform {
        val clionPath = providers.gradleProperty("clionPath")
        if (clionPath.isPresent) local(clionPath.get()) else clion("2026.3")
    }
}

// The version's entry of CHANGELOG.md, its "- " items as a list: the Marketplace's change notes.
val changeNotesHtml = run {
    // (the version a local: the configuration cache keeps the lambda below, which may not reach the script's own values)
    val notesVersion = version.toString()
    providers.fileContents(layout.projectDirectory.file("CHANGELOG.md")).asText.map { text ->
        val entry = text.substringAfter("## $notesVersion\n", "").substringBefore("\n## ")
        Regex("""^- (.*(?:\n  .*)*)""", RegexOption.MULTILINE).findAll(entry).joinToString("", "<ul>", "</ul>") { item ->
            val line = item.groupValues[1].replace(Regex("""\s*\n\s+"""), " ").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            "<li>" + line.replace(Regex("`([^`]+)`"), "<code>$1</code>") + "</li>"
        }
    }
}

intellijPlatform {
    pluginConfiguration {
        ideaVersion { sinceBuild = "263" }
        changeNotes = changeNotesHtml
    }
    buildSearchableOptions = false
    // ./gradlew verifyPlugin: JetBrains' Plugin Verifier against the CLion built with (its APIs as used).
    pluginVerification {
        ides {
            val clionPath = providers.gradleProperty("clionPath")
            if (clionPath.isPresent) local(file(clionPath.get())) else create("CL", "2026.3")
        }
    }
}
