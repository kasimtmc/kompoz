// kompozc --kate: KDE's Kate has no extension store; a language comes to it as a highlighting definition
// (KSyntaxHighlighting's XML, in its data directory) and a language server in its LSP client's settings
// (kate/lspclient/settings.json in its configuration directory). This writes both: kate/kompoz-cpp.xml,
// built into kompozc, and a "kompoz" server, this kompozc --lsp, for files Kate highlights as Kompoz C++;
// the settings' other servers are kept.

#include "kate.hpp"

#include "json.hpp"
#include "kate_syntax.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace kompozc {

namespace {

std::optional<fs::path> environment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return std::nullopt;
    return fs::path(value);
}

// Where Kate (Qt's standard paths) looks: its highlighting definitions under the data directory, its
// settings under the configuration one.
struct KatePaths {
    fs::path data, config;
};

std::optional<KatePaths> katePaths() {
#if defined(_WIN32)
    auto local = environment("LOCALAPPDATA");
    if (!local) return std::nullopt;
    return KatePaths{*local, *local};
#elif defined(__APPLE__)
    auto home = environment("HOME");
    if (!home) return std::nullopt;
    return KatePaths{*home / "Library/Application Support", *home / "Library/Preferences"};
#else
    auto home = environment("HOME");
    auto data = environment("XDG_DATA_HOME"), config = environment("XDG_CONFIG_HOME");
    if (!home && (!data || !config)) return std::nullopt;
    return KatePaths{data ? *data : *home / ".local/share", config ? *config : *home / ".config"};
#endif
}

// This kompozc: argv[0] as a path, or found on PATH as a shell finds it.
fs::path self(const char* argv0) {
    fs::path given = argv0;
    if (given.has_parent_path()) return fs::weakly_canonical(fs::absolute(given));
    if (auto path = environment("PATH")) {
#if defined(_WIN32)
        const char separator = ';';
        if (!given.has_extension()) given += ".exe";
#else
        const char separator = ':';
#endif
        std::stringstream dirs(path->string());
        for (std::string dir; std::getline(dirs, dir, separator);) {
            std::error_code ec;
            if (!dir.empty() && fs::is_regular_file(fs::path(dir) / given, ec)) return fs::weakly_canonical(fs::path(dir) / given);
        }
    }
    return given;
}

bool write(const fs::path& file, const std::string& text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << text;
    return bool(out.flush());
}

}  // namespace

int installKate(const char* argv0) {
    auto paths = katePaths();
    if (!paths) {
        std::cerr << "kompozc: no home directory to find Kate's in\n";
        return 1;
    }
    fs::path syntax = paths->data / "org.kde.syntax-highlighting" / "syntax" / "kompoz-cpp.xml";
    fs::path settings = paths->config / "kate" / "lspclient" / "settings.json";

    // The settings as they are (the user's own servers in them), the "kompoz" server set.
    Json config = Json::Object();
    std::error_code ec;
    if (fs::exists(settings, ec)) {
        std::ifstream in(settings, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        try {
            if (text.str().find_first_not_of(" \t\r\n") != std::string::npos) config = Json::parse(text.str());
        } catch (const std::exception& e) {
            std::cerr << "kompozc: " << settings.string() << " isn't JSON (" << e.what() << "); left as it is\n";
            return 1;
        }
        if (!config.isObject()) {
            std::cerr << "kompozc: " << settings.string() << " isn't a JSON object; left as it is\n";
            return 1;
        }
    }
    std::string kompozc = self(argv0).string();
    Json server;
    server["command"] = Json::Array{kompozc, "--lsp", "--background-index"};
    server["highlightingModeRegex"] = "^Kompoz C\\+\\+$";
    server["documentLanguageId"] = false;  // (kompozc gives clangd "cpp", Kate would give "kompoz")
    server["url"] = "https://kasimtmc.github.io/kompoz/en/tools.html";
    config["servers"]["kompoz"] = std::move(server);

    if (!write(syntax, kateSyntax)) {
        std::cerr << "kompozc: can't write " << syntax.string() << "\n";
        return 1;
    }
    if (!write(settings, config.dump(2) + "\n")) {
        std::cerr << "kompozc: can't write " << settings.string() << "\n";
        return 1;
    }
    std::cout << "Kate's highlighting for Kompoz C++ (.cpp, .cc and .cxx files): " << syntax.string() << "\n"
              << "Kompoz's language server, " << kompozc << " --lsp, for them: " << settings.string() << "\n"
              << "Restart Kate; it asks once whether to start the server.\n";
    return 0;
}

}  // namespace kompozc
