#pragma once

// kompozc's language server (kompozc --lsp): lsp.cpp says what it does.

#include <string>
#include <vector>

namespace kompozc {

// Serves an editor on stdin and stdout until it says exit; `args`: what followed --lsp.
int runLanguageServer(const std::vector<std::string>& args);

}  // namespace kompozc
