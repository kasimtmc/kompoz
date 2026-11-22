// kompozc --lsp: a language server for C++ sources with @Kompozable functions in Kotlin's
// syntax. It stands in front of clangd: a plain C++ file goes to clangd as it is; one with Kotlin
// goes to clangd as C++ tools can read it (maskedView: the Kotlin blanked, what C++ needs of it
// on its first line), and its translation goes to clangd too, under the translated file's own
// name (whose compile command the build wrote). What clangd finds in the C++ around the Kotlin
// comes back as it is; what it finds in the translation comes back on the Kotlin's own lines (the
// translation's #line directives say which); what kompozc finds wrong in the Kotlin comes back
// where it is. Inside the Kotlin, clangd's other answers (completion, hover, ...) are not given.
//
//   kompozc --lsp [--clangd PATH] [clangd's arguments...]
//
// Editors start it where they would start clangd (VS Code's clangd extension: clangd.path, and
// --lsp first in clangd.arguments). Without a clangd it still gives kompozc's own diagnostics.

#include "lsp.hpp"

#include "json.hpp"
#include "lexer.hpp"
#include "translate.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace kompozc {

namespace {

// ---- A child process with pipes (clangd) ------------------------------------------------------

class Child {
public:
    ~Child() { closeInput(); }
    bool start(const std::vector<std::string>& argv) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE inRead, inWrite, outRead, outWrite;
        if (!CreatePipe(&inRead, &inWrite, &sa, 0)) return false;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) return false;
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        std::wstring command;
        for (const std::string& a : argv) {
            if (!command.empty()) command += L' ';
            std::wstring w(a.size() + 1, L'\0');
            w.resize(size_t(MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, w.data(), int(w.size()))) - 1);
            command += L'"';
            for (wchar_t c : w) {
                if (c == L'"') command += L'\\';
                command += c;
            }
            command += L'"';
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};
        bool ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        if (!ok) {
            CloseHandle(inWrite);
            CloseHandle(outRead);
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        in_ = inWrite;
        out_ = outRead;
        return true;
#else
        int in[2], out[2], failed[2];  // (failed: closed by a successful exec, written to when it fails)
        if (pipe(in) != 0 || pipe(out) != 0 || pipe(failed) != 0) return false;
        fcntl(failed[1], F_SETFD, FD_CLOEXEC);
        pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            dup2(in[0], 0);
            dup2(out[1], 1);
            close(in[0]), close(in[1]), close(out[0]), close(out[1]), close(failed[0]);
            std::vector<char*> args;
            for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
            args.push_back(nullptr);
            execvp(args[0], args.data());
            char byte = 1;
            (void)!::write(failed[1], &byte, 1);
            _exit(127);
        }
        close(in[0]);
        close(out[1]);
        close(failed[1]);
        char byte;
        bool execFailed = ::read(failed[0], &byte, 1) > 0;
        close(failed[0]);
        if (execFailed) {
            close(in[1]), close(out[0]);
            waitpid(pid, nullptr, 0);
            return false;
        }
        pid_ = pid;
        in_ = in[1];
        out_ = out[0];
        // (the child gone, a write gives an error, not the signal)
        std::signal(SIGPIPE, SIG_IGN);
        return true;
#endif
    }
    bool write(const std::string& bytes) {
        size_t done = 0;
        while (done < bytes.size()) {
#ifdef _WIN32
            DWORD n = 0;
            if (!WriteFile(in_, bytes.data() + done, DWORD(bytes.size() - done), &n, nullptr)) return false;
#else
            ssize_t n = ::write(in_, bytes.data() + done, bytes.size() - done);
            if (n <= 0) return false;
#endif
            done += size_t(n);
        }
        return true;
    }
    // Some bytes; 0 at its end.
    size_t read(char* buffer, size_t size) {
#ifdef _WIN32
        DWORD n = 0;
        if (!ReadFile(out_, buffer, DWORD(size), &n, nullptr)) return 0;
        return n;
#else
        ssize_t n = ::read(out_, buffer, size);
        return n > 0 ? size_t(n) : 0;
#endif
    }
    void closeInput() {
#ifdef _WIN32
        if (in_) CloseHandle(in_), in_ = nullptr;
#else
        if (in_ >= 0) close(in_), in_ = -1;
#endif
    }
    void wait() {
#ifdef _WIN32
        if (process_) WaitForSingleObject(process_, 5000), CloseHandle(process_), process_ = nullptr;
#else
        if (pid_ > 0) waitpid(pid_, nullptr, 0), pid_ = -1;
#endif
    }

private:
#ifdef _WIN32
    HANDLE process_ = nullptr, in_ = nullptr, out_ = nullptr;
#else
    pid_t pid_ = -1;
    int in_ = -1, out_ = -1;
#endif
};

// ---- Messages: Content-Length framed JSON -----------------------------------------------------

class Framer {
public:
    explicit Framer(std::function<size_t(char*, size_t)> source) : source_(std::move(source)) {}
    // The next message's body; nothing at the end of the input.
    std::optional<std::string> next() {
        for (;;) {
            size_t headers = buffer_.find("\r\n\r\n");
            if (headers != std::string::npos) {
                size_t length = 0;
                std::istringstream lines(buffer_.substr(0, headers));
                for (std::string line; std::getline(lines, line);) {
                    if (line.size() > 15 && std::equal(line.begin(), line.begin() + 15, "Content-Length:", [](char a, char b) {
                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                        }))
                        length = size_t(std::stoul(line.substr(15)));
                }
                size_t begin = headers + 4;
                while (buffer_.size() < begin + length)
                    if (!fill()) return std::nullopt;
                std::string body = buffer_.substr(begin, length);
                buffer_.erase(0, begin + length);
                return body;
            }
            if (!fill()) return std::nullopt;
        }
    }

private:
    bool fill() {
        char chunk[65536];
        size_t n = source_(chunk, sizeof chunk);
        if (n == 0) return false;
        buffer_.append(chunk, n);
        return true;
    }
    std::function<size_t(char*, size_t)> source_;
    std::string buffer_;
};

std::string framed(const Json& message) {
    std::string body = message.dump();
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

// Messages written by a thread of their own: whoever sends never waits on a pipe the other side
// isn't reading yet (clangd and the editor each write while they read).
class Outbox {
public:
    explicit Outbox(std::function<bool(const std::string&)> write) : write_(std::move(write)), thread_([this] { loop(); }) {}
    ~Outbox() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    void send(std::string bytes) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(bytes));
        }
        wake_.notify_all();
    }

private:
    void loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;  // (stopping, all written)
            std::string bytes = std::move(queue_.front());
            queue_.erase(queue_.begin());
            lock.unlock();
            bool ok = write_(bytes);
            lock.lock();
            if (!ok) queue_.clear();
        }
    }
    std::function<bool(const std::string&)> write_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::string> queue_;
    bool stopping_ = false;
    std::thread thread_;
};

// ---- URIs and positions -----------------------------------------------------------------------

std::string uriToPath(const std::string& uri) {
    std::string_view rest = uri;
    if (rest.starts_with("file://")) rest.remove_prefix(7);
    std::string path;
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            path += char(std::stoi(std::string(rest.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            path += rest[i];
        }
    }
#ifdef _WIN32
    if (path.size() > 2 && path[0] == '/' && path[2] == ':') path.erase(0, 1);  // /C:/x
#endif
    return path;
}

std::string pathToUri(const fs::path& p) {
    std::string path = p.generic_string();
    std::string uri = "file://";
#ifdef _WIN32
    uri += '/';
#endif
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr("/-_.~:", c)) uri += char(c);
        else uri += '%', uri += hex[c >> 4], uri += hex[c & 15];
    }
    return uri;
}

// The same file, however its path is written (an editor's URI decoded, the build's path).
std::string fileKey(const fs::path& p) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    std::string s = (ec ? fs::absolute(p).lexically_normal() : c).generic_string();
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
#endif
    return s;
}

// A text's lines, for LSP positions (lines, and columns in UTF-16 code units) and byte offsets.
struct Lines {
    explicit Lines(std::string_view text) : text(text) {
        starts.push_back(0);
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] == '\n') starts.push_back(i + 1);
    }
    size_t offset(int line, int character) const {
        if (line < 0) return 0;
        if (size_t(line) >= starts.size()) return text.size();
        size_t i = starts[size_t(line)];
        int units = 0;
        while (i < text.size() && text[i] != '\n' && units < character) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            units += len == 4 ? 2 : 1;
            i += len;
        }
        return std::min(i, text.size());
    }
    // The UTF-16 column of a byte offset in its line.
    int column(int line, size_t offset) const {
        int units = 0;
        for (size_t i = starts[size_t(line)]; i < offset && i < text.size();) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            units += len == 4 ? 2 : 1;
            i += len;
        }
        return units;
    }
    int lineOf(size_t offset) const { return int(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin()) - 1; }
    // A line's text from its first character that isn't a space to its end.
    Json wholeLine(int line) const {
        if (line < 0 || size_t(line) >= starts.size()) line = int(starts.size()) - 1;
        size_t b = starts[size_t(line)];
        size_t e = text.find('\n', b);
        if (e == std::string_view::npos) e = text.size();
        size_t first = b;
        while (first < e && (text[first] == ' ' || text[first] == '\t')) ++first;
        if (e > b && text[e - 1] == '\r') --e;
        return range(line, column(line, first), line, column(line, e));
    }
    static Json position(int line, int character) {
        Json p;
        p["line"] = line;
        p["character"] = character;
        return p;
    }
    static Json range(int l0, int c0, int l1, int c1) {
        Json r;
        r["start"] = position(l0, c0);
        r["end"] = position(l1, c1);
        return r;
    }
    std::string_view text;
    std::vector<size_t> starts;
};

// ---- The build's commands ---------------------------------------------------------------------

// How the build translates a source (kompozc_commands.json, written by kompozable).
struct Translation {
    std::string output;  // the translated file, the compile command's
    std::vector<std::string> headers, includes, modules;
};

struct BuildDir {
    fs::path dir;
    fs::file_time_type compileStamp{}, kompozcStamp{};
    std::map<std::string, Json> compileCommands;      // by the file's canonical path
    std::map<std::string, Translation> translations;  // by the source's canonical path
};

// A compile command's arguments ("arguments", or "command" split as a shell would).
std::vector<std::string> argumentsOf(const Json& entry) {
    std::vector<std::string> args;
    if (entry["arguments"].isArray()) {
        for (const Json& a : entry["arguments"].array()) args.push_back(a.string());
        return args;
    }
    const std::string& command = entry["command"].string();
    std::string current;
    bool any = false;
    char quote = 0;
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < command.size()) current += command[++i];
            else current += c;
        } else if (c == '"' || c == '\'') {
            quote = c, any = true;
        } else if (c == '\\' && i + 1 < command.size()) {
            current += command[++i], any = true;
        } else if (c == ' ' || c == '\t') {
            if (any || !current.empty()) args.push_back(current), current.clear(), any = false;
        } else {
            current += c;
        }
    }
    if (any || !current.empty()) args.push_back(current);
    return args;
}

// ---- The server -------------------------------------------------------------------------------

struct Doc {
    std::string uri, path, lineFile;  // lineFile: as kompozc's #line names it
    std::string text;
    int version = 0;
    bool kotlin = false;
    std::vector<std::pair<size_t, size_t>> regions;  // the Kotlin declarations, byte offsets
    Json kotlinDiagnostics = Json::Array(), maskedDiagnostics = Json::Array(), translatedDiagnostics = Json::Array();
    std::optional<Translation> translation;
    BuildDir* build = nullptr;
    bool located = false;  // (looked for once)
    bool configured = false;  // clangd told the masked view's compile command
    bool openInClangd = false;
    // The translation, open in clangd under its own file's name.
    std::string translatedUri;
    int translatedVersion = 0;
    std::vector<std::pair<std::string, int>> lineMap;  // each translated line's source file and line (1-based)
    uint64_t generation = 0;

    bool inKotlin(size_t offset) const {
        return std::any_of(regions.begin(), regions.end(), [&](const auto& r) { return offset >= r.first && offset < r.second; });
    }
    bool inKotlin(const Json& position) const {
        if (!kotlin) return false;
        Lines lines(text);
        return inKotlin(lines.offset(position["line"].integer(), position["character"].integer()));
    }
    // Whether a source line (0-based) has Kotlin on it.
    bool kotlinLine(int line) const {
        if (!kotlin) return false;
        Lines lines(text);
        return std::any_of(regions.begin(), regions.end(), [&](const auto& r) {
            return line >= lines.lineOf(r.first) && line <= lines.lineOf(r.second == 0 ? 0 : r.second - 1);
        });
    }
};

// ---- The Kotlin's semantic tokens ----
// clangd's tokens are kept off the Kotlin (its view there is blank); the Kotlin's are kompozc's own, read
// with its lexer: keywords, strings (their templates' code as code), numbers, comments, annotations, and
// what names are by where they stand: functions and classes declared and called, values declared,
// parameters, properties after a dot, named arguments. Merged into one answer in clangd's legend, with the
// types it lacks added to it.

// The token types and modifiers by LSP's names, an index each (a modifier: a bit).
struct Legend {
    std::vector<std::string> types, modifiers;
    int type(const std::string& name) {
        auto it = std::find(types.begin(), types.end(), name);
        if (it != types.end()) return int(it - types.begin());
        types.push_back(name);
        return int(types.size()) - 1;
    }
    int modifier(const std::string& name) {
        auto it = std::find(modifiers.begin(), modifiers.end(), name);
        if (it == modifiers.end()) it = modifiers.insert(modifiers.end(), name);
        return 1 << int(it - modifiers.begin());
    }
    // The types and modifiers the Kotlin's tokens use, there from the start (a legend can't grow once told).
    void addKotlin() {
        for (const char* t : {"keyword", "string", "number", "comment", "decorator", "function", "class", "interface", "parameter", "variable",
                              "property", "label", "type"})
            type(t);
        modifier("declaration");
    }
    Json json() const {
        Json j;
        Json::Array t, m;
        for (const std::string& s : types) t.push_back(Json(s));
        for (const std::string& s : modifiers) m.push_back(Json(s));
        j["tokenTypes"] = Json(std::move(t));
        j["tokenModifiers"] = Json(std::move(m));
        return j;
    }
};

struct SemanticToken {
    int line, start, length, type, modifiers;
    friend bool operator<(const SemanticToken& a, const SemanticToken& b) { return std::tie(a.line, a.start) < std::tie(b.line, b.start); }
};

namespace kotlin_tokens {

bool hardKeyword(std::string_view w) {
    static const std::set<std::string, std::less<>> words = {
        "as", "break", "class", "continue", "do", "else", "false", "for", "fun", "if", "in", "interface", "is", "null", "object",
        "package", "return", "super", "this", "throw", "true", "try", "typealias", "typeof", "val", "var", "when", "while",
        "catch", "finally", "import"};
    return words.contains(w);
}
// Kotlin's soft keywords and modifiers: keywords where a declaration or a name follows (data class,
// override fun, by lazy, companion object); names elsewhere (a value called data).
bool softKeyword(std::string_view w) {
    static const std::set<std::string, std::less<>> words = {
        "abstract", "actual", "annotation", "by", "companion", "const", "constructor", "crossinline", "data", "enum", "expect",
        "external", "field", "final", "get", "infix", "init", "inline", "inner", "internal", "lateinit", "noinline", "open",
        "operator", "out", "override", "private", "protected", "public", "reified", "sealed", "set", "suspend", "tailrec",
        "vararg", "value", "where"};
    return words.contains(w);
}

struct Reader {
    const std::string& text;
    const Lines& lines;
    Legend& legend;
    std::vector<SemanticToken>& out;
    int declaration;
    // The values and functions the regions declare (a first reading): a name's kind where it is used.
    std::set<std::string, std::less<>> values, functions;
    bool collecting = false;

    // A token over [from, to): one a line, as LSP's tokens can't go across lines.
    void add(size_t from, size_t to, const char* type, int modifiers = 0) {
        if (collecting) return;
        int t = legend.type(type);
        while (from < to) {
            size_t nl = text.find('\n', from);
            size_t end = nl == std::string::npos ? to : std::min(to, nl);
            if (end > from) {
                int line = lines.lineOf(from), start = lines.column(line, from);
                out.push_back({line, start, lines.column(line, end) - start, t, modifiers});
            }
            from = end + 1;
        }
    }

    // The comments between two tokens (what the lexer skips: blanks and comments).
    void comments(size_t from, size_t to) {
        while (from < to) {
            size_t slash = text.find('/', from);
            if (slash == std::string::npos || slash + 1 >= to) return;
            if (text[slash + 1] == '/') {
                size_t end = std::min(to, text.find('\n', slash) == std::string::npos ? to : text.find('\n', slash));
                add(slash, end, "comment");
                from = end;
            } else if (text[slash + 1] == '*') {
                size_t close = text.find("*/", slash + 2);
                size_t end = close == std::string::npos ? to : std::min(to, close + 2);
                add(slash, end, "comment");
                from = end;
            } else {
                from = slash + 1;
            }
        }
    }

    // The tokens of [from, to), lexed from `from` (a region, or a template's code).
    void read(size_t from, size_t to) {
        std::vector<Token> tokens;
        try {
            Pos start;
            start.line = lines.lineOf(from) + 1;
            start.offset = from;
            start.column = int(from - lines.starts[size_t(start.line - 1)]) + 1;
            Lexer lexer(text, start);
            for (;;) {
                Token t = lexer.next();
                if (t.kind == Tok::End || t.pos.offset >= to) break;
                tokens.push_back(std::move(t));
            }
        } catch (const CompileError&) {
            // (what is read up to the mistake keeps its tokens)
        }
        size_t last = from;
        std::vector<Token> code;  // (but the line ends)
        for (Token& t : tokens) {
            comments(last, t.pos.offset);
            last = t.end.offset;
            if (t.kind != Tok::Newline) code.push_back(std::move(t));
        }
        comments(last, to);
        classify(code);
    }

    void classify(const std::vector<Token>& code) {
        auto op = [&](size_t i, std::string_view o) { return i < code.size() && code[i].kind == Tok::Op && code[i].text == o; };
        auto ident = [&](size_t i) { return i < code.size() && code[i].kind == Tok::Ident; };
        // The names a lambda's arrow follows ({ a, b -> }, { (x, y) -> }), from the brace on.
        std::set<size_t> lambdaParameters;
        for (size_t i = 0; i < code.size(); ++i) {
            if (!op(i, "{")) continue;
            std::vector<size_t> names;
            size_t j = i + 1;
            while (j < code.size() && (ident(j) || op(j, ",") || op(j, "(") || op(j, ")") || op(j, ":") || op(j, "<") || op(j, ">") || op(j, ".") ||
                                       op(j, "?"))) {
                if (ident(j) && !(j > 0 && (op(j - 1, ":") || op(j - 1, "<") || op(j - 1, ".")))) names.push_back(j);
                ++j;
            }
            if (op(j, "->")) lambdaParameters.insert(names.begin(), names.end());
        }
        enum class After { None, Fun, Parameters, Class, Value } after = After::None;
        int parameterDepth = -1, depth = 0;  // a fun declaration's parentheses: its parameters
        for (size_t i = 0; i < code.size(); ++i) {
            const Token& t = code[i];
            size_t from = t.pos.offset, to = t.end.offset;
            switch (t.kind) {
            case Tok::Number: add(from, to, "number"); break;
            case Tok::Char: add(from, to, "string"); break;
            case Tok::String: string(t); break;
            case Tok::Op:
                if (t.text == "(") {
                    if (after == After::Parameters) parameterDepth = depth, after = After::None;
                    ++depth;
                } else if (t.text == ")") {
                    if (--depth == parameterDepth) parameterDepth = -1;
                } else if (t.text == "@" && ident(i + 1) && code[i + 1].pos.offset == to) {
                    bool label = i > 0 && ident(i - 1) && code[i - 1].end.offset == from;  // return@name
                    if (label) {
                        add(code[i + 1].pos.offset, code[i + 1].end.offset, "label");
                    } else {  // an annotation, its dotted name too
                        size_t end = code[i + 1].end.offset;
                        size_t j = i + 1;
                        while (op(j + 1, ".") && ident(j + 2)) j += 2, end = code[j].end.offset;
                        add(from, end, "decorator");
                        i = j;
                    }
                }
                break;
            case Tok::Ident: {
                const std::string& w = t.text;
                bool call = op(i + 1, "(") || op(i + 1, "{") || (op(i + 1, "<") && callAfterTypeArguments(code, i + 1));
                if (hardKeyword(w) && !(i > 0 && op(i - 1, "."))) {
                    add(from, to, "keyword");
                    after = w == "fun" ? After::Fun : (w == "class" || w == "interface" || w == "object" || w == "typealias") ? After::Class
                          : (w == "val" || w == "var") ? After::Value : after;
                    if (w == "this" || w == "super") after = After::None;
                    break;
                }
                if (softKeyword(w) && !(i > 0 && op(i - 1, ".")) &&
                    (ident(i + 1) || ((w == "get" || w == "set" || w == "constructor") && op(i + 1, "(")) || (w == "init" && op(i + 1, "{")))) {
                    add(from, to, "keyword");
                    break;
                }
                if (after == After::Fun) {  // fun <T> Receiver.name(: the last name before the parameters
                    if (op(i + 1, ".")) {
                        add(from, to, "class");
                    } else {
                        add(from, to, "function", declaration);
                        if (collecting) functions.insert(w);
                        after = After::Parameters;
                    }
                    break;
                }
                if (after == After::Class) {
                    add(from, to, "class", declaration);
                    after = After::None;
                    break;
                }
                if (after == After::Value) {  // val Receiver.name, val (a, b)
                    if (op(i + 1, ".")) {
                        add(from, to, "class");
                    } else {
                        add(from, to, "variable", declaration);
                        if (collecting) values.insert(w);
                        if (!op(i + 1, ",")) after = After::None;
                    }
                    break;
                }
                if (lambdaParameters.contains(i) || (parameterDepth >= 0 && depth == parameterDepth + 1 && op(i + 1, ":"))) {
                    add(from, to, "parameter", declaration);
                } else if (w == "it" && !op(i + 1, ":")) {
                    add(from, to, "parameter");
                } else if (call || (functions.contains(w) && op(i - 1, "::"))) {
                    add(from, to, "function");
                } else if (values.contains(w) && !(i > 0 && (op(i - 1, ".") || op(i - 1, "?.")))) {
                    add(from, to, "variable");  // a value the file declares
                } else if (op(i + 1, "=") && i > 0 && (op(i - 1, "(") || op(i - 1, ","))) {
                    add(from, to, "parameter");  // a named argument
                } else if (i > 0 && (op(i - 1, ".") || op(i - 1, "?."))) {
                    add(from, to, std::isupper(static_cast<unsigned char>(w[0])) ? "class" : "property");
                } else if (std::isupper(static_cast<unsigned char>(w[0]))) {
                    add(from, to, "class");
                }
                break;
            }
            default: break;
            }
        }
    }

    // name<…>( or name<…> { : the angle brackets close on the same line before a call's parenthesis or lambda.
    static bool callAfterTypeArguments(const std::vector<Token>& code, size_t lt) {
        int open = 0;
        for (size_t j = lt; j < code.size(); ++j) {
            const Token& t = code[j];
            if (t.kind == Tok::Op && t.text == "<") ++open;
            else if (t.kind == Tok::Op && t.text == ">") {
                if (--open == 0) return j + 1 < code.size() && code[j + 1].kind == Tok::Op && (code[j + 1].text == "(" || code[j + 1].text == "{");
            } else if (t.kind != Tok::Ident && !(t.kind == Tok::Op && (t.text == "," || t.text == "." || t.text == "?" || t.text == "*"))) {
                return false;
            }
        }
        return false;
    }

    // A string: its text, and the code of its templates read as code.
    void string(const Token& t) {
        size_t at = t.pos.offset;
        for (const StringPart& part : t.parts) {
            if (!part.expr) continue;
            bool braced = part.pos.offset >= 2 && text[part.pos.offset - 1] == '{';
            size_t open = part.pos.offset - (braced ? 2 : 1), close = part.pos.offset + part.text.size() + (braced ? 1 : 0);
            add(at, open, "string");
            if (braced) {
                read(part.pos.offset, part.pos.offset + part.text.size());
            } else {
                Token name;
                name.kind = Tok::Ident;
                name.text = part.text;
                name.pos = part.pos;
                name.end = part.pos;
                name.end.offset += part.text.size();
                std::vector<Token> one{name};
                classify(one);
            }
            at = close;
        }
        add(at, t.end.offset, "string");
    }
};

}  // namespace kotlin_tokens

// The Kotlin regions' tokens of `text`.
std::vector<SemanticToken> kotlinTokens(const std::string& text, const std::vector<std::pair<size_t, size_t>>& regions, Legend& legend) {
    Lines lines(text);
    std::vector<SemanticToken> out;
    kotlin_tokens::Reader reader{text, lines, legend, out, legend.modifier("declaration")};
    reader.collecting = true;  // (first the names declared, then the tokens)
    for (auto [begin, end] : regions) reader.read(begin, end);
    reader.collecting = false;
    for (auto [begin, end] : regions) reader.read(begin, end);
    std::sort(out.begin(), out.end());
    return out;
}

// Tokens as LSP sends them: each relative to the one before.
Json::Array encodeTokens(const std::vector<SemanticToken>& tokens) {
    Json::Array data;
    int line = 0, start = 0;
    for (const SemanticToken& t : tokens) {
        data.push_back(Json(t.line - line));
        data.push_back(Json(t.line == line ? t.start - start : t.start));
        data.push_back(Json(t.length));
        data.push_back(Json(t.type));
        data.push_back(Json(t.modifiers));
        line = t.line, start = t.start;
    }
    return data;
}

class Server {
public:
    Server(std::string clangd, std::vector<std::string> clangdArgs, std::optional<fs::path> compileCommandsDir)
        : clangdPath_(std::move(clangd)), clangdArgs_(std::move(clangdArgs)), compileCommandsDir_(std::move(compileCommandsDir)) {}

    int run() {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::vector<std::string> argv{clangdPath_};
        argv.insert(argv.end(), clangdArgs_.begin(), clangdArgs_.end());
        haveClangd_ = clangd_.start(argv);
        if (!haveClangd_) std::cerr << "kompozc: no clangd (" << clangdPath_ << "): kompozc's own diagnostics only\n";
        editorOut_.emplace([](const std::string& bytes) {
            bool ok = std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
            std::fflush(stdout);
            return ok;
        });
        clangdOut_.emplace([this](const std::string& bytes) { return clangd_.write(bytes); });
        std::thread fromClangd;
        if (haveClangd_) fromClangd = std::thread([this] { readClangd(); });
        std::thread translator([this] { translateLoop(); });
        // (what has come, not a buffer's worth: fread would wait for one)
        Framer editor([](char* b, size_t n) -> size_t {
#ifdef _WIN32
            DWORD got = 0;
            return ReadFile(GetStdHandle(STD_INPUT_HANDLE), b, DWORD(n), &got, nullptr) ? size_t(got) : 0;
#else
            ssize_t got = ::read(0, b, n);
            return got > 0 ? size_t(got) : 0;
#endif
        });
        while (auto body = editor.next()) {
            Json message;
            try {
                message = Json::parse(*body);
            } catch (const std::exception&) {
                continue;
            }
            if (!fromEditor(message)) break;
        }
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        translator.join();
        clangdOut_.reset();  // (what is left written first)
        if (haveClangd_) {
            clangd_.closeInput();
            fromClangd.join();
            clangd_.wait();
        }
        editorOut_.reset();
        return 0;
    }

private:
    // ---- writing

    void toEditor(const Json& message) { editorOut_->send(framed(message)); }
    void toClangd(const Json& message) {
        if (haveClangd_) clangdOut_->send(framed(message));
    }
    static Json notification(const std::string& method, Json params) {
        Json m;
        m["jsonrpc"] = "2.0";
        m["method"] = method;
        m["params"] = std::move(params);
        return m;
    }
    static Json response(const Json& id, Json result) {
        Json m;
        m["jsonrpc"] = "2.0";
        m["id"] = id;
        m["result"] = std::move(result);
        return m;
    }

    // ---- the editor's messages; false: exit

    bool fromEditor(Json message) {
        const Json& in = message;  // (read through: a missing member read isn't added)
        const std::string& method = in["method"].string();
        bool request = message.has("id") && !method.empty();
        if (method.empty()) {  // a response, to clangd's request
            toClangd(message);
            return true;
        }
        if (method == "exit") {
            toClangd(message);
            return false;
        }
        if (method == "initialize") {
            // Positions in UTF-16, LSP's own: what the text and the translation are measured in here.
            Json& caps = message["params"]["capabilities"];
            if (caps.has("general")) caps["general"].erase("positionEncodings");
            caps.erase("offsetEncoding");
            if (!haveClangd_) {
                Json result;
                result["capabilities"]["textDocumentSync"]["openClose"] = true;
                result["capabilities"]["textDocumentSync"]["change"] = 1;
                result["capabilities"]["textDocumentSync"]["save"] = true;
                {
                    std::lock_guard lock(mutex_);
                    legend_ = Legend();
                    legend_.addKotlin();
                    result["capabilities"]["semanticTokensProvider"]["legend"] = legend_.json();
                }
                result["capabilities"]["semanticTokensProvider"]["full"] = true;
                result["serverInfo"]["name"] = "kompozc";
                toEditor(response(in["id"], std::move(result)));
                return true;
            }
            remember(in["id"], method, "");
            toClangd(message);
            return true;
        }
        if (method == "shutdown" && !haveClangd_) {
            toEditor(response(in["id"], Json()));
            return true;
        }
        if (method == "textDocument/didOpen") {
            const Json& d = in["params"]["textDocument"];
            std::lock_guard lock(mutex_);
            if (virtualUris_.contains(d["uri"].string())) return true;  // (a translation open as the editor's)
            Doc& doc = docs_[d["uri"].string()];
            doc.uri = d["uri"].string();
            doc.path = uriToPath(doc.uri);
            doc.lineFile = fs::absolute(doc.path).lexically_normal().generic_string();
            doc.text = d["text"].string();
            doc.version = d["version"].integer();
            changed(doc, d["languageId"].string());
            return true;
        }
        if (method == "textDocument/didChange") {
            std::lock_guard lock(mutex_);
            auto it = docs_.find(in["params"]["textDocument"]["uri"].string());
            if (it == docs_.end()) return true;
            Doc& doc = it->second;
            for (const Json& change : in["params"]["contentChanges"].array()) {
                if (!change.has("range")) {
                    doc.text = change["text"].string();
                    continue;
                }
                Lines lines(doc.text);
                const Json& r = change["range"];
                size_t b = lines.offset(r["start"]["line"].integer(), r["start"]["character"].integer());
                size_t e = lines.offset(r["end"]["line"].integer(), r["end"]["character"].integer());
                doc.text.replace(b, e - b, change["text"].string());
            }
            doc.version = in["params"]["textDocument"]["version"].integer(doc.version + 1);
            changed(doc, "");
            return true;
        }
        if (method == "textDocument/didClose") {
            std::lock_guard lock(mutex_);
            auto it = docs_.find(in["params"]["textDocument"]["uri"].string());
            if (it == docs_.end()) return true;
            Doc& doc = it->second;
            if (doc.openInClangd) toClangd(message);
            if (!doc.translatedUri.empty()) {
                Json p;
                p["textDocument"]["uri"] = doc.translatedUri;
                toClangd(notification("textDocument/didClose", p));
                virtualUris_.erase(doc.translatedUri);
            }
            Json clear;
            clear["uri"] = doc.uri;
            clear["diagnostics"] = Json::Array();
            toEditor(notification("textDocument/publishDiagnostics", clear));
            docs_.erase(it);
            return true;
        }
        if (method == "textDocument/didSave") {
            std::lock_guard lock(mutex_);
            if (auto it = docs_.find(in["params"]["textDocument"]["uri"].string()); it != docs_.end() && it->second.openInClangd)
                toClangd(message);
            // (another source of the module saved: the open ones' translations again)
            for (auto& [uri, doc] : docs_)
                if (doc.kotlin) schedule(doc);
            return true;
        }
        // A request about a place in a document.
        const std::string& uri = in["params"]["textDocument"]["uri"].string();
        if (!uri.empty() && request) {
            std::lock_guard lock(mutex_);
            auto it = docs_.find(uri);
            if (it != docs_.end() && it->second.kotlin) {
                const Doc& doc = it->second;
                // Formatting: clang-format would rewrite the Kotlin as C++.
                if (method == "textDocument/formatting" || method == "textDocument/rangeFormatting" ||
                    method == "textDocument/onTypeFormatting" || method == "textDocument/rangesFormatting") {
                    toEditor(response(in["id"], Json()));
                    return true;
                }
                // Inside the Kotlin: nothing of clangd's (its view there is blank).
                if (in["params"].has("position") && doc.inKotlin(in["params"]["position"])) {
                    Json empty;
                    if (method == "textDocument/completion") empty["isIncomplete"] = false, empty["items"] = Json::Array();
                    else if (method == "textDocument/references" || method == "textDocument/documentHighlight") empty = Json::Array();
                    toEditor(response(in["id"], empty));
                    return true;
                }
            }
        }
        if (!haveClangd_) {
            if (method == "textDocument/semanticTokens/full") {  // (the Kotlin's alone)
                Json result;
                std::lock_guard lock(mutex_);
                auto it = docs_.find(uri);
                result["data"] = Json(it == docs_.end() ? Json::Array() : encodeTokens(kotlinTokens(it->second.text, it->second.regions, legend_)));
                toEditor(response(in["id"], std::move(result)));
                return true;
            }
            if (request) toEditor(response(in["id"], Json()));
            return true;
        }
        if (request) remember(in["id"], method, uri);
        toClangd(message);
        return true;
    }

    void remember(const Json& id, const std::string& method, const std::string& uri) {
        std::lock_guard lock(pendingMutex_);
        pending_[id.dump()] = {method, uri};
    }

    // A document's text changed (or it was opened): its view for clangd, its Kotlin's errors.
    void changed(Doc& doc, const std::string& languageId) {
        ++doc.generation;
        std::vector<CompileError> errors;
        std::vector<Found> found;
        try {
            found = scan(doc.text, nullptr, &errors);
        } catch (const CompileError& e) {  // (not a declaration's: the scan itself)
            errors.push_back(e);
        }
        doc.kotlin = !found.empty();
        doc.regions.clear();
        for (const Found& f : found) doc.regions.emplace_back(f.begin, f.end);
        doc.kotlinDiagnostics = Json::Array();
        Lines lines(doc.text);
        for (const CompileError& e : errors) doc.kotlinDiagnostics.array().push_back(diagnostic(lines, e));
        if (doc.kotlin && !std::exchange(doc.located, true)) locate(doc);
        std::string view = doc.text;
        if (doc.kotlin) {
            try {
                // (the module's other sources, as the editor has them: what this one's C++ uses of their Kotlin too)
                std::vector<OtherSource> others;
                if (doc.translation) {
                    std::map<std::string, std::string> open;
                    for (const auto& [u, d] : docs_) open[fileKey(d.path)] = d.text;
                    others = readOthers(doc.translation->modules, doc.path, [&](const fs::path& p) -> std::optional<std::string> {
                        auto o = open.find(fileKey(p));
                        return o == open.end() ? std::nullopt : std::optional(o->second);
                    });
                }
                view = maskedView(doc.text, found, structsFor(doc), others);
            } catch (const CompileError&) {
                view = maskedView(doc.text, {}, Structs());
            }
            if (!doc.configured && doc.translation) configure(doc);
        }
        if (!doc.openInClangd) {
            Json p;
            p["textDocument"]["uri"] = doc.uri;
            p["textDocument"]["languageId"] = languageId.empty() ? "cpp" : languageId;
            p["textDocument"]["version"] = doc.version;
            p["textDocument"]["text"] = view;
            toClangd(notification("textDocument/didOpen", p));
            doc.openInClangd = true;
        } else {
            Json p;
            p["textDocument"]["uri"] = doc.uri;
            p["textDocument"]["version"] = doc.version;
            Json whole;
            whole["text"] = view;
            p["contentChanges"] = Json::Array{whole};
            toClangd(notification("textDocument/didChange", p));
        }
        if (doc.kotlin) {
            schedule(doc);
            publish(doc);
        } else if (!doc.translatedDiagnostics.array().empty() || !doc.kotlinDiagnostics.array().empty() || !haveClangd_) {
            doc.translatedDiagnostics = Json::Array();
            publish(doc);
        }
    }

    Json diagnostic(const Lines& lines, const CompileError& e) {
        int line = std::max(0, e.pos.line - 1);
        // (kompozc's column counts characters; LSP's, UTF-16 units)
        size_t at = lines.starts[std::min(size_t(line), lines.starts.size() - 1)];
        for (int c = 1; c < e.pos.column && at < lines.text.size() && lines.text[at] != '\n';) {
            ++at;
            while (at < lines.text.size() && (static_cast<unsigned char>(lines.text[at]) & 0xC0) == 0x80) ++at;
            ++c;
        }
        int column = lines.column(line, at);
        Json d;
        d["range"] = Lines::range(line, column, line, column + 1);
        d["severity"] = 1;
        d["source"] = "kompozc";
        d["message"] = e.what();
        return d;
    }

    // ---- where the build is

    BuildDir* buildFor(const fs::path& source) {
        std::vector<fs::path> candidates;
        if (compileCommandsDir_) {
            candidates.push_back(*compileCommandsDir_);
        } else {
            // The source's directory and those above it, and their build directories (build/,
            // CLion's cmake-build-debug/, ...: any directory in them with kompozc_commands.json).
            for (fs::path dir = fs::absolute(source).parent_path();; dir = dir.parent_path()) {
                candidates.push_back(dir);
                std::error_code ec;
                for (const auto& sub : fs::directory_iterator(dir, ec))
                    if (std::error_code e; sub.is_directory(e) && fs::exists(sub.path() / "kompozc_commands.json", e)) candidates.push_back(sub.path());
                if (dir == dir.parent_path()) break;
            }
        }
        for (const fs::path& dir : candidates) {
            std::error_code ec;
            if (!fs::exists(dir / "compile_commands.json", ec)) continue;
            BuildDir& b = builds_[fileKey(dir)];
            b.dir = dir;
            reload(b);
            if (b.translations.contains(fileKey(source)) || compileCommandsDir_) return &b;
        }
        return nullptr;
    }
    static Json readJson(const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        try {
            return Json::parse(text.str());
        } catch (const std::exception&) {
            return Json();
        }
    }
    void reload(BuildDir& b) {
        std::error_code ec;
        auto compileStamp = fs::last_write_time(b.dir / "compile_commands.json", ec);
        if (!ec && compileStamp != b.compileStamp) {
            b.compileStamp = compileStamp;
            b.compileCommands.clear();
            Json compile = readJson(b.dir / "compile_commands.json");  // (kept: the loop reads into it)
            for (const Json& entry : compile.array()) {
                fs::path file = entry["file"].string();
                if (file.is_relative()) file = fs::path(entry["directory"].string()) / file;
                b.compileCommands[fileKey(file)] = entry;
            }
        }
        auto kompozcStamp = fs::last_write_time(b.dir / "kompozc_commands.json", ec);
        if (!ec && kompozcStamp != b.kompozcStamp) {
            b.kompozcStamp = kompozcStamp;
            b.translations.clear();
            Json kompozc = readJson(b.dir / "kompozc_commands.json");  // (kept: the loop reads into it)
            for (const Json& entry : kompozc.array()) {
                Translation t;
                t.output = entry["output"].string();
                for (const Json& h : entry["headers"].array()) t.headers.push_back(h.string());
                for (const Json& i : entry["includes"].array()) t.includes.push_back(i.string());
                for (const Json& m : entry["modules"].array()) t.modules.push_back(m.string());
                b.translations[fileKey(entry["source"].string())] = std::move(t);
            }
        }
    }
    void locate(Doc& doc) {
        doc.build = buildFor(doc.path);
        if (!doc.build) return;
        if (auto t = doc.build->translations.find(fileKey(doc.path)); t != doc.build->translations.end()) {
            doc.translation = t->second;
            doc.translatedUri = pathToUri(t->second.output);
            virtualUris_.insert(doc.translatedUri);
        }
    }
    // Kompoz's structs for a document's translation (read once for each set of header directories).
    const Structs& structsFor(const Doc& doc) {
        static const Structs none;
        if (!doc.translation) return none;
        std::string key;
        for (const std::string& h : doc.translation->headers) key += h + '\n';
        key += '\n';
        for (const std::string& i : doc.translation->includes) key += i + '\n';
        auto it = structs_.find(key);
        if (it == structs_.end()) it = structs_.emplace(key, readStructs(doc.translation->headers, doc.translation->includes)).first;
        return it->second;
    }
    // clangd's compile commands for the masked view (the translation's, of this file) and for the
    // translation, with the compiler's language standard written out when the command leaves it to
    // the compiler's default (CMake does where that default will do, as GCC 16's C++20): clangd
    // would take its own, C++17.
    void configure(Doc& doc) {
        doc.configured = true;
        auto entry = doc.build->compileCommands.find(fileKey(doc.translation->output));
        if (entry == doc.build->compileCommands.end()) return;
        std::vector<std::string> args = argumentsOf(entry->second);
        if (args.empty()) return;
        const std::string& directory = entry->second["directory"].string();
        bool written = std::any_of(args.begin(), args.end(), [](const std::string& a) {
            return a.starts_with("-std=") || a.starts_with("--std=") || a.starts_with("-std:") || a.starts_with("/std:");
        });
        std::string standard = written ? std::string() : defaultStandard(args[0], directory);
        std::string output = fileKey(doc.translation->output);
        Json masked = Json::Array(), translated = Json::Array();
        for (size_t i = 0; i < args.size(); ++i) {
            fs::path a = args[i];
            if (!a.empty() && a.is_relative()) a = fs::path(directory) / a;
            masked.array().push_back(fileKey(a) == output ? doc.path : args[i]);
            translated.array().push_back(args[i]);
            if (i == 0 && !standard.empty()) masked.array().push_back(standard), translated.array().push_back(standard);
        }
        Json p;
        p["settings"]["compilationDatabaseChanges"][doc.path]["workingDirectory"] = directory;
        p["settings"]["compilationDatabaseChanges"][doc.path]["compilationCommand"] = std::move(masked);
        if (!standard.empty()) {
            p["settings"]["compilationDatabaseChanges"][doc.translation->output]["workingDirectory"] = directory;
            p["settings"]["compilationDatabaseChanges"][doc.translation->output]["compilationCommand"] = std::move(translated);
        }
        toClangd(notification("workspace/didChangeConfiguration", p));
    }
    // The standard a compiler takes by default, as its option (-std=gnu++20), from the __cplusplus it
    // defines (asked once for each compiler); none for MSVC's cl and clang-cl, or if it can't be asked.
    std::string defaultStandard(const std::string& compiler, const std::string& directory) {
        fs::path driver = compiler;
        if (driver.has_parent_path() && driver.is_relative()) driver = fs::path(directory) / driver;
        std::string name = driver.stem().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (name == "cl" || name == "clang-cl") return {};
        auto known = defaultStandards_.find(driver.string());
        if (known != defaultStandards_.end()) return known->second;
        std::string defines;
        Child child;
        if (child.start({driver.string(), "-dM", "-E", "-x", "c++", "-"})) {
            child.closeInput();
            char buffer[4096];
            for (size_t n; (n = child.read(buffer, sizeof buffer)) > 0;) defines.append(buffer, n);
            child.wait();
        }
        std::string standard;
        if (size_t at = defines.find("#define __cplusplus "); at != std::string::npos) {
            long value = std::atol(defines.c_str() + at + 20);
            const char* year = value > 202302 ? "26" : value > 202002 ? "23" : value > 201703 ? "20" : value > 201402 ? "17" : value > 201103 ? "14" : "11";
            standard = (defines.find("#define __STRICT_ANSI__") != std::string::npos ? "-std=c++" : "-std=gnu++") + std::string(year);
        }
        return defaultStandards_[driver.string()] = standard;
    }

    // ---- translations, in the background (a moment after the last change)

    void schedule(Doc& doc) {
        due_[doc.uri] = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        wake_.notify_all();
    }
    void translateLoop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            if (stopping_) return;
            if (due_.empty()) {
                wake_.wait(lock);
                continue;
            }
            auto next = std::min_element(due_.begin(), due_.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            if (next->second > std::chrono::steady_clock::now()) {
                wake_.wait_until(lock, next->second);
                continue;
            }
            std::string uri = next->first;
            due_.erase(next);
            auto it = docs_.find(uri);
            if (it == docs_.end() || !it->second.kotlin) continue;
            Doc& doc = it->second;
            if (!doc.translation) {  // (not a source of a kompozable target the build knows: its errors alone)
                publish(doc);
                continue;
            }
            // What it needs, copied: translated without the lock.
            std::string text = doc.text, path = doc.path, lineFile = doc.lineFile;
            Translation translation = *doc.translation;
            Structs structs = structsFor(doc);
            uint64_t generation = doc.generation;
            std::map<std::string, std::string> open;  // the module's sources as the editor has them
            for (const auto& [u, d] : docs_) open[fileKey(d.path)] = d.text;
            lock.unlock();
            std::string translated;
            std::optional<CompileError> error;
            try {
                structs.readIncludes(text, fs::path(path).parent_path());
                structs.readText(text);
                std::vector<OtherSource> others = readOthers(translation.modules, path, [&](const fs::path& p) -> std::optional<std::string> {
                    auto o = open.find(fileKey(p));
                    return o == open.end() ? std::nullopt : std::optional(o->second);
                });
                translated = translate(text, path, lineFile, structs, others);
            } catch (const CompileError& e) {
                error = e;
            }
            lock.lock();
            it = docs_.find(uri);
            if (it == docs_.end() || it->second.generation != generation) continue;  // (changed again: translated again)
            Doc& now = it->second;
            if (error) {
                now.translatedDiagnostics = Json::Array();  // (the last translation's: its lines may be gone)
                Lines lines(now.text);
                Json d = diagnostic(lines, *error);
                bool known = false;
                for (const Json& k : now.kotlinDiagnostics.array()) known = known || k["range"] == d["range"];
                if (!known) now.kotlinDiagnostics.array().push_back(d);
                publish(now);
                continue;
            }
            now.lineMap = lineMap(translated);
            Json p;
            p["textDocument"]["uri"] = now.translatedUri;
            p["textDocument"]["version"] = ++now.translatedVersion;
            if (now.translatedVersion == 1) {
                p["textDocument"]["languageId"] = "cpp";
                p["textDocument"]["text"] = translated;
                toClangd(notification("textDocument/didOpen", p));
            } else {
                Json whole;
                whole["text"] = translated;
                p["contentChanges"] = Json::Array{whole};
                toClangd(notification("textDocument/didChange", p));
            }
            if (!haveClangd_) publish(now);
        }
    }
    // Each line of a translation: the source file and line it stands for (#line N "file").
    static std::vector<std::pair<std::string, int>> lineMap(const std::string& translated) {
        std::vector<std::pair<std::string, int>> map;
        std::string file;
        int next = 0;
        std::istringstream lines(translated);
        for (std::string line; std::getline(lines, line);) {
            size_t b = line.find_first_not_of(" \t");
            if (b != std::string::npos && line.compare(b, 5, "#line") == 0) {
                std::istringstream directive(line.substr(b + 5));
                directive >> next;
                size_t q = line.find('"');
                if (q != std::string::npos) file = line.substr(q + 1, line.rfind('"') - q - 1);
                map.emplace_back(std::string(), 0);
                continue;
            }
            map.emplace_back(file, next > 0 ? next : 0);
            if (next > 0) ++next;
        }
        return map;
    }

    // ---- clangd's messages

    void readClangd() {
        Framer from([this](char* b, size_t n) { return clangd_.read(b, n); });
        while (auto body = from.next()) {
            Json message;
            try {
                message = Json::parse(*body);
            } catch (const std::exception&) {
                continue;
            }
            fromClangd(std::move(message));
        }
    }
    void fromClangd(Json message) {
        const Json& in = message;  // (read through: a missing member read isn't added)
        const std::string& method = in["method"].string();
        if (method == "textDocument/publishDiagnostics") {
            const std::string& uri = in["params"]["uri"].string();
            std::lock_guard lock(mutex_);
            if (auto it = docs_.find(uri); it != docs_.end() && it->second.kotlin) {
                it->second.maskedDiagnostics = in["params"]["diagnostics"];
                publish(it->second);
                return;
            }
            for (auto& [u, doc] : docs_)
                if (doc.translatedUri == uri) {
                    doc.translatedDiagnostics = mapBack(doc, in["params"]["diagnostics"]);
                    publish(doc);
                    return;
                }
            if (virtualUris_.contains(uri)) return;
            toEditor(message);
            return;
        }
        if (!method.empty() || !message.has("id")) {  // clangd's request or notification: the editor's to answer
            toEditor(message);
            return;
        }
        std::pair<std::string, std::string> what;
        {
            std::lock_guard lock(pendingMutex_);
            auto it = pending_.find(in["id"].dump());
            if (it != pending_.end()) what = it->second, pending_.erase(it);
        }
        if (what.first == "initialize") {
            // Semantic tokens whole (a delta can't be kept off the Kotlin, nor a range's be given the Kotlin's), in
            // clangd's legend with the types the Kotlin's tokens need.
            const Json& provider = in["result"]["capabilities"]["semanticTokensProvider"];
            if (provider.isObject()) {
                std::lock_guard lock(mutex_);
                legend_ = Legend();
                for (const Json& t : provider["legend"]["tokenTypes"].array()) legend_.types.push_back(t.string());
                for (const Json& m : provider["legend"]["tokenModifiers"].array()) legend_.modifiers.push_back(m.string());
                legend_.addKotlin();
                Json& mine = message["result"]["capabilities"]["semanticTokensProvider"];
                mine["legend"] = legend_.json();
                mine["full"] = true;
                mine.erase("range");
            }
            if (in.has("result")) message["result"]["serverInfo"]["name"] = "kompozc (clangd)";
        } else if (!what.second.empty() && in.has("result")) {
            std::lock_guard lock(mutex_);
            if (auto it = docs_.find(what.second); it != docs_.end() && it->second.kotlin) offKotlin(it->second, what.first, message["result"]);
        }
        toEditor(message);
    }

    // A translation's diagnostics on the source's lines: those of its Kotlin (the C++ around it is
    // the masked view's to report).
    Json mapBack(const Doc& doc, const Json& diagnostics) {
        Lines lines(doc.text);
        Json out = Json::Array();
        std::set<std::pair<int, std::string>> seen;
        for (const Json& d : diagnostics.array()) {
            int line = d["range"]["start"]["line"].integer();
            if (line < 0 || size_t(line) >= doc.lineMap.size()) continue;
            const auto& [file, sourceLine] = doc.lineMap[size_t(line)];
            if (sourceLine <= 0 || fileKey(file) != fileKey(doc.path) || !doc.kotlinLine(sourceLine - 1)) continue;
            if (!seen.insert({sourceLine, d["message"].string()}).second) continue;
            Json m;
            m["range"] = lines.wholeLine(sourceLine - 1);
            m["severity"] = d["severity"];
            m["source"] = "kompozc (C++)";
            m["message"] = d["message"];
            if (d.has("code")) m["code"] = d["code"];
            out.array().push_back(std::move(m));
        }
        return out;
    }

    void publish(Doc& doc) {
        Json all = Json::Array();
        Lines lines(doc.text);
        std::set<std::string> seen;
        auto add = [&](const Json& d) {
            if (seen.insert(d["range"].dump() + d["message"].string()).second) all.array().push_back(d);
        };
        for (const Json& d : doc.maskedDiagnostics.array()) {
            if (doc.kotlin) {
                if (doc.inKotlin(lines.offset(d["range"]["start"]["line"].integer(), d["range"]["start"]["character"].integer()))) continue;
                // (its includes are the Kotlin's too, which clangd's view doesn't have: not unused, not missing)
                const std::string& code = d["code"].string();
                if (code == "unused-includes" || code == "missing-includes") continue;
            }
            add(d);
        }
        for (const Json& d : doc.translatedDiagnostics.array()) add(d);
        for (const Json& d : doc.kotlinDiagnostics.array()) add(d);
        Json p;
        p["uri"] = doc.uri;
        p["version"] = doc.version;
        p["diagnostics"] = std::move(all);
        toEditor(notification("textDocument/publishDiagnostics", p));
    }

    // clangd's answer for a document with Kotlin, kept off the Kotlin: tokens, hints and edits there
    // were found in its blank view.
    void offKotlin(const Doc& doc, const std::string& method, Json& result) {
        Lines lines(doc.text);
        auto inKotlin = [&](const Json& position) {
            return doc.inKotlin(lines.offset(position["line"].integer(), position["character"].integer()));
        };
        if (method == "textDocument/semanticTokens/full" || method == "textDocument/semanticTokens/range") {
            const Json& read = result;
            std::vector<SemanticToken> tokens = kotlinTokens(doc.text, doc.regions, legend_);  // (the Kotlin's)
            int line = 0, start = 0;
            if (read["data"].isArray()) {
                const Json::Array& data = read["data"].array();
                for (size_t i = 0; i + 4 < data.size(); i += 5) {
                    int dl = int(data[i].integer()), ds = int(data[i + 1].integer());
                    line += dl;
                    start = dl ? ds : start + ds;
                    if (doc.inKotlin(lines.offset(line, start))) continue;  // (clangd's are kept off it)
                    tokens.push_back({line, start, int(data[i + 2].integer()), int(data[i + 3].integer()), int(data[i + 4].integer())});
                }
            }
            std::sort(tokens.begin(), tokens.end());
            result["data"] = Json(encodeTokens(tokens));
        } else if (method == "textDocument/inlayHint") {
            Json::Array kept;
            for (Json& h : result.array())
                if (!inKotlin(std::as_const(h)["position"])) kept.push_back(std::move(h));
            result = Json(std::move(kept));
        } else if (method == "textDocument/documentLink" && result.isArray()) {
            // (the masked view's #include on a Kotlin line: its link would underline the Kotlin there)
            Json::Array kept;
            for (Json& l : result.array())
                if (!inKotlin(std::as_const(l)["range"]["start"])) kept.push_back(std::move(l));
            result = Json(std::move(kept));
        } else {
            // Edits anywhere in the answer (a rename, a code action's): none inside the Kotlin.
            std::function<void(Json&)> walk = [&](Json& v) {
                if (v.isObject()) {
                    const Json& read = v;  // (read through: a missing member read isn't added)
                    if (read["changes"].isObject())
                        if (auto c = v["changes"].object().find(doc.uri); c != v["changes"].object().end()) {
                            Json::Array kept;
                            for (Json& e : c->second.array())
                                if (!inKotlin(std::as_const(e)["range"]["start"]) && !inKotlin(std::as_const(e)["range"]["end"])) kept.push_back(std::move(e));
                            c->second = Json(std::move(kept));
                        }
                    if (read["textDocument"]["uri"].string() == doc.uri && read["edits"].isArray()) {
                        Json::Array kept;
                        for (Json& e : v["edits"].array())
                            if (!inKotlin(std::as_const(e)["range"]["start"]) && !inKotlin(std::as_const(e)["range"]["end"])) kept.push_back(std::move(e));
                        v["edits"] = Json(std::move(kept));
                    }
                    for (auto& [k, child] : v.object()) walk(child);
                } else if (v.isArray()) {
                    for (Json& child : v.array()) walk(child);
                }
            };
            walk(result);
        }
    }

    std::string clangdPath_;
    std::vector<std::string> clangdArgs_;
    std::optional<fs::path> compileCommandsDir_;
    Child clangd_;
    bool haveClangd_ = false;
    Legend legend_;  // the semantic tokens' (clangd's, with the Kotlin's types)

    std::mutex mutex_;  // the documents, the builds, the translations due
    std::map<std::string, Doc> docs_;
    std::map<std::string, BuildDir> builds_;
    std::map<std::string, Structs> structs_;
    std::map<std::string, std::string> defaultStandards_;  // by compiler
    std::set<std::string> virtualUris_;
    std::map<std::string, std::chrono::steady_clock::time_point> due_;
    std::condition_variable wake_;
    bool stopping_ = false;

    std::mutex pendingMutex_;
    std::map<std::string, std::pair<std::string, std::string>> pending_;  // the editor's requests: method, document

    std::optional<Outbox> editorOut_, clangdOut_;  // (last: gone first, their threads with them)
};

}  // namespace

int runLanguageServer(const std::vector<std::string>& args) {
    std::string clangd = "clangd";
    std::vector<std::string> clangdArgs;
    std::optional<fs::path> compileCommandsDir;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--clangd" && i + 1 < args.size()) {
            clangd = args[++i];
        } else if (a.starts_with("--clangd=")) {
            clangd = a.substr(9);
        } else {
            if (a.starts_with("--compile-commands-dir=")) compileCommandsDir = fs::path(a.substr(23));
            clangdArgs.push_back(a);  // (clangd's own, passed on)
        }
    }
    return Server(clangd, clangdArgs, compileCommandsDir).run();
}

}  // namespace kompozc
