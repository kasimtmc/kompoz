import { test } from "node:test";
import * as assert from "node:assert/strict";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { hasKotlin, mayBeKompozable } from "../src/kompozable";
import { findKompozc, serverArguments } from "../src/kompozc";

test("C++ sources with Kotlin declarations are told from plain C++", () => {
    assert.ok(mayBeKompozable("/p/screen.cpp") && mayBeKompozable("a.CC") && mayBeKompozable("b.cxx"));
    assert.ok(!mayBeKompozable("/p/screen.hpp") && !mayBeKompozable("main.kt") && !mayBeKompozable("Makefile"));
    assert.ok(hasKotlin('#include "x.hpp"\n\n@Kompozable\nfun Screen() {\n    Text("a")\n}\n'));
    assert.ok(hasKotlin("val InkBlue = Color(0xFF1E88E5)\n"));
    assert.ok(hasKotlin("private fun helper() = 1\n") && hasKotlin("object Routes\n") && hasKotlin("data class P(val x: Int)\n"));
    assert.ok(hasKotlin("@Serializable\ndata class Item(val id: Int)\n"));
    // C++ that looks a little like it: none at a line's start.
    assert.ok(!hasKotlin("int main() {\n    auto fun = 1;  // fun at no line's start\n}\n"));
    assert.ok(!hasKotlin("class Value { int v; };\nstruct object_ref {};\n// @Kompozable in a comment\n"));
});

test("kompozc is the setting's, else KOMPOZC's, else the newest built in the workspace, else on PATH", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "kompoz-vscode-"));
    try {
        const where = { setting: "", env: {}, folders: [root], windows: false };
        assert.equal(findKompozc(where), "kompozc");
        assert.equal(findKompozc({ ...where, windows: true }), "kompozc.exe");
        const built = (dir: string, seconds: number) => {
            fs.mkdirSync(path.join(root, dir, "compiler"), { recursive: true });
            const file = path.join(root, dir, "compiler", "kompozc");
            fs.writeFileSync(file, "#!/bin/sh\n", { mode: 0o755 });
            fs.utimesSync(file, seconds, seconds);
            return file;
        };
        built("build", 1000);
        const newer = built("cmake-build-debug", 2000);
        fs.mkdirSync(path.join(root, "other", "compiler"), { recursive: true });  // (not a build directory)
        fs.writeFileSync(path.join(root, "other", "compiler", "kompozc"), "", { mode: 0o755 });
        assert.equal(findKompozc(where), newer);
        assert.equal(findKompozc({ ...where, env: { KOMPOZC: "/opt/kompozc" } }), "/opt/kompozc");
        assert.equal(findKompozc({ ...where, setting: " /x/kompozc ", env: { KOMPOZC: "/opt/kompozc" } }), "/x/kompozc");
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test("the server is started with --lsp, the clangd it is given, then clangd's arguments", () => {
    assert.deepEqual(serverArguments("", []), ["--lsp"]);
    assert.deepEqual(serverArguments("/usr/bin/clangd-19", ["--header-insertion=never"]), ["--lsp", "--clangd", "/usr/bin/clangd-19", "--header-insertion=never"]);
});
