// Kompoz's plugin for CLion and Android Studio. Built against an IDE of yours (-PidePath=/path/to/clion or
// /path/to/android-studio: no download) or against CLion 2026.3 downloaded:
//
//   JAVA_HOME=/path/to/clion/jbr ./gradlew buildPlugin -PidePath=/path/to/clion
//
// (Gradle needs a JDK: the IDE's own, its jbr/, will do.)
// build/distributions/kompoz-clion-studio-*.zip is then installed in the IDE from disk (Settings, Plugins). A tag
// editors-v<version> releases it with the VS Code extension, whose version is the same, on GitHub and JetBrains
// Marketplace (.github/workflows/editors-release.yml).

plugins {
    id("org.jetbrains.kotlin.jvm") version "2.4.21"
    id("org.jetbrains.intellij.platform") version "2.20.1"
}

group = "com.plyg"
version = "0.3.0"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    intellijPlatform {
        val idePath = providers.gradleProperty("idePath")
        if (idePath.isPresent) local(idePath.get()) else clion("2026.3")
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
        ideaVersion { sinceBuild = "262" }
        changeNotes = changeNotesHtml
    }
    buildSearchableOptions = false
    // ./gradlew verifyPlugin: JetBrains' Plugin Verifier against the IDE built with (its APIs as used), or against
    // CLion 2026.3 and Android Studio 2026.2.2 (a 262 build: the oldest line the plugin takes, sinceBuild) downloaded.
    pluginVerification {
        ides {
            val idePath = providers.gradleProperty("idePath")
            if (idePath.isPresent) {
                local(file(idePath.get()))
            } else {
                create("CL", "2026.3")
                create("AI", "2026.2.2.10")
            }
        }
    }
}
