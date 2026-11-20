// Kompoz in VS Code. A C++ source with Kotlin code in it (Kompoz's kompozable: @Kompozable
// functions in Kotlin's syntax) is shown as "Kompoz C++": the Kotlin highlighted as Kotlin, and
// the C/C++ and clangd extensions, which would read it as broken C++, leave it alone. kompozc's
// language server, kompozc --lsp, takes it instead: kompozc's errors and clangd's on their lines,
// clangd's answers in the C++ around the Kotlin. Plain C++ sources stay C++.

import * as vscode from "vscode";
import { LanguageClient, LanguageClientOptions, ServerOptions, State } from "vscode-languageclient/node";
import { hasKotlin, mayBeKompozable } from "./kompozable";
import { findKompozc, serverArguments } from "./kompozc";

const language = "kompoz-cpp";
let client: LanguageClient | undefined;
let output: vscode.OutputChannel;
const pending = new Map<string, NodeJS.Timeout>();  // a document's check after its edits, by URI

export function activate(context: vscode.ExtensionContext): void {
    output = vscode.window.createOutputChannel("Kompoz");
    context.subscriptions.push(
        output,
        vscode.workspace.onDidOpenTextDocument(check),
        // (After a pause in the typing: a file gains or loses its Kotlin.)
        vscode.workspace.onDidChangeTextDocument((e) => {
            const key = e.document.uri.toString();
            clearTimeout(pending.get(key));
            pending.set(key, setTimeout(() => {
                pending.delete(key);
                check(e.document);
            }, 500));
        }),
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration("kompoz") && client) void restart();
        }),
        vscode.commands.registerCommand("kompoz.restartServer", () => restart()),
        { dispose: () => void client?.stop() },
    );
    vscode.workspace.textDocuments.forEach(check);
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}

/** A C++ source shown as Kompoz C++ if it has Kotlin, and back as C++ if it no longer has. */
function check(document: vscode.TextDocument): void {
    if (document.uri.scheme !== "file" || !mayBeKompozable(document.fileName)) return;
    const kotlin = hasKotlin(document.getText());
    if (document.languageId === "cpp" && kotlin) {
        output.appendLine(`${document.fileName} has Kotlin code: Kompoz C++`);
        void vscode.languages.setTextDocumentLanguage(document, language);
    } else if (document.languageId === language && !kotlin) {
        output.appendLine(`${document.fileName} has no Kotlin code left: C++`);
        void vscode.languages.setTextDocumentLanguage(document, "cpp");
    }
    if (document.languageId === language || (document.languageId === "cpp" && kotlin)) void start();
}

async function start(): Promise<void> {
    if (client) return;
    const settings = vscode.workspace.getConfiguration("kompoz");
    const kompozc = findKompozc({
        setting: settings.get<string>("kompozcPath", ""),
        env: process.env,
        folders: (vscode.workspace.workspaceFolders ?? []).map((f) => f.uri.fsPath),
        windows: process.platform === "win32",
    });
    const args = serverArguments(settings.get<string>("clangdPath", ""), settings.get<string[]>("clangdArguments", []));
    output.appendLine(`starting ${kompozc} ${args.join(" ")}`);
    const server: ServerOptions = { command: kompozc, args, options: { cwd: vscode.workspace.workspaceFolders?.[0]?.uri.fsPath } };
    const options: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language }],
        outputChannel: output,
        middleware: {
            // (clangd behind it takes the file as C++)
            didOpen: (document, next) => next(asCpp(document)),
        },
    };
    client = new LanguageClient("kompoz", "Kompoz (kompozc --lsp)", server, options);
    client.onDidChangeState((e) => {
        if (e.newState === State.Stopped) output.appendLine("kompozc --lsp stopped");
    });
    try {
        await client.start();
    } catch (error) {
        client = undefined;
        const message = `Kompoz: ${kompozc} --lsp didn't start (${error instanceof Error ? error.message : error}).`;
        const choice = await vscode.window.showErrorMessage(message, "Settings");
        if (choice) void vscode.commands.executeCommand("workbench.action.openSettings", "kompoz.kompozcPath");
    }
}

async function restart(): Promise<void> {
    const old = client;
    client = undefined;
    await old?.stop();
    if (vscode.workspace.textDocuments.some((d) => d.languageId === language)) await start();
}

// The document as one of language "cpp" (what is read of it otherwise as it is).
function asCpp(document: vscode.TextDocument): vscode.TextDocument {
    return new Proxy(document, {
        get: (target, property) => (property === "languageId" ? "cpp" : Reflect.get(target, property, target)),
    });
}
