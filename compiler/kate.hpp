#pragma once

// kompozc --kate: KDE's Kate set up for Kompoz's sources (kate.cpp).

namespace kompozc {

// Installs Kate's highlighting for Kompoz C++ and adds kompozc's language server (this kompozc, found by
// argv0) to Kate's language server settings. 0 when done.
int installKate(const char* argv0);

}  // namespace kompozc
