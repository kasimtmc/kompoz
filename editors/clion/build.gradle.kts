// Kompoz's CLion plugin. Built against a CLion of yours (-PclionPath=/path/to/clion: no download) or
// against CLion 2026.3 downloaded:
//
//   JAVA_HOME=/path/to/clion/jbr ./gradlew buildPlugin -PclionPath=/path/to/clion
//
// (Gradle needs a JDK: CLion's own, its jbr/, will do.)
// build/distributions/kompoz-clion-*.zip is then installed in CLion from disk (Settings, Plugins).

plugins {
    id("org.jetbrains.kotlin.jvm") version "2.4.21"
    id("org.jetbrains.intellij.platform") version "2.20.1"
}

group = "dev.kompoz"
version = "0.1.2"

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

intellijPlatform {
    pluginConfiguration {
        ideaVersion { sinceBuild = "263" }
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
