// Which sources are C++ with Kotlin code in them (Kompoz's kompozable): a line that starts a Kotlin
// declaration no C++ line starts. The same test as the CLion plugin's (KompozLsp.kt).

const extensions = new Set(["cpp", "cc", "cxx"]);
const kotlinDeclaration = /^(@Kompozable\b|@Serializable\b|(private |internal )?(fun|val|var) |object \w|data class )/m;

/** Whether a file of this name may hold Kotlin (a C++ source's extension). */
export function mayBeKompozable(fileName: string): boolean {
    const dot = fileName.lastIndexOf(".");
    return dot >= 0 && extensions.has(fileName.slice(dot + 1).toLowerCase());
}

/** Whether this C++ source's text has Kotlin code. */
export function hasKotlin(text: string): boolean {
    return kotlinDeclaration.test(text);
}
