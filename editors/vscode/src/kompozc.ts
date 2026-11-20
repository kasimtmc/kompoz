// Where kompozc is, as the CLion plugin finds it (KompozSettings.kt): the setting; else KOMPOZC from
// the environment; else one built in a workspace folder (build/ or cmake-build-*/compiler/kompozc,
// the newest); else kompozc on PATH.

import * as fs from "fs";
import * as path from "path";

export interface Where {
    setting: string;
    env: NodeJS.ProcessEnv;
    folders: string[];  // the workspace folders' paths
    windows: boolean;
}

export function findKompozc(where: Where): string {
    if (where.setting.trim()) return where.setting.trim();
    const fromEnv = where.env.KOMPOZC;
    if (fromEnv) return fromEnv;
    const name = where.windows ? "kompozc.exe" : "kompozc";
    let newest: { file: string; time: number } | undefined;
    for (const folder of where.folders) {
        let entries: fs.Dirent[];
        try {
            entries = fs.readdirSync(folder, { withFileTypes: true });
        } catch {
            continue;
        }
        for (const entry of entries) {
            if (!entry.isDirectory() || !(entry.name === "build" || entry.name.startsWith("cmake-build-"))) continue;
            const file = path.join(folder, entry.name, "compiler", name);
            try {
                fs.accessSync(file, fs.constants.X_OK);
                const time = fs.statSync(file).mtimeMs;
                if (!newest || time > newest.time) newest = { file, time };
            } catch {
                // (not built there)
            }
        }
    }
    return newest?.file ?? name;
}

/** kompozc's arguments as a language server: --lsp, the clangd to start, clangd's own. */
export function serverArguments(clangdPath: string, clangdArguments: string[]): string[] {
    const args = ["--lsp"];
    if (clangdPath.trim()) args.push("--clangd", clangdPath.trim());
    return args.concat(clangdArguments);
}
