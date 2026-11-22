#!/usr/bin/env python3
"""kompozc's language server (kompozc --lsp), driven as an editor drives it, on tests/kompozable_tests_other.cpp
(a source of kompozable_tests, so the build's kompozc_commands.json knows its translation).

    python3 tests/kompozc_lsp_test.py --kompozc build/compiler/kompozc [--clangd clangd-19 | --no-clangd]

With clangd: the file is clean as it is; an error in the Kotlin's C++ comes back on its Kotlin line, one in the
C++ around it where it is, kompozc's own where it is; inside the Kotlin, clangd's hover and completion are not
given, and formatting isn't done; a compile command that leaves the language standard to the compiler's default
still gives the compiler's. Without clangd: kompozc's own errors. Exits 1 on a failed check."""

import argparse, json, os, queue, shlex, subprocess, sys, tempfile, threading, time

ap = argparse.ArgumentParser()
ap.add_argument('--kompozc', required=True)
ap.add_argument('--clangd', default='clangd')
ap.add_argument('--no-clangd', action='store_true')
args = ap.parse_args()

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(root, 'tests', 'kompozable_tests_other.cpp')
uri = 'file://' + path
command = [args.kompozc, '--lsp', '--clangd', '/nonexistent/clangd' if args.no_clangd else args.clangd, '--background-index=false']
failures = 0


def reader(f, inbox):
    while True:
        headers = b''
        while not headers.endswith(b'\r\n\r\n'):
            c = f.read(1)
            if not c:
                return
            headers += c
        length = next(int(l.split(b':')[1]) for l in headers.split(b'\r\n') if l.lower().startswith(b'content-length'))
        inbox.put(json.loads(f.read(length)))


def start(extra=()):
    """A server (the one the checks below talk to), from the initialize request to the file opened."""
    global server, inbox
    server = subprocess.Popen(command + list(extra), cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    inbox = queue.Queue()
    threading.Thread(target=reader, args=(server.stdout, inbox), daemon=True).start()
    latest['diagnostics'] = None
    initialized = request('initialize', {'processId': None, 'rootUri': 'file://' + root, 'capabilities': {}})
    send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
    send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'cpp', 'version': 1, 'text': text}}})
    return initialized


def stop():
    request('shutdown', None)
    send({'jsonrpc': '2.0', 'method': 'exit'})
    try:
        return server.wait(timeout=20)
    except subprocess.TimeoutExpired:
        server.kill()
        return None


next_id = [100]


def send(message):
    body = json.dumps(message).encode()
    server.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    server.stdin.flush()


def request(method, params):
    next_id[0] += 1
    rid = next_id[0]
    send({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params})
    deadline = time.time() + 60
    while time.time() < deadline:
        try:
            m = inbox.get(timeout=1)
        except queue.Empty:
            continue
        if m.get('id') == rid and 'method' not in m:
            return m.get('result')
        diagnostics(m)
    raise SystemExit(f'no answer to {method}')


latest = {'diagnostics': None}


def diagnostics(m):
    if m.get('method') == 'textDocument/publishDiagnostics' and m['params']['uri'] == uri:
        latest['diagnostics'] = m['params']['diagnostics']


def check(what, ok):
    global failures
    print(('[ OK ] ' if ok else '[FAIL] ') + what)
    if not ok:
        failures += 1


def wait_for(what, condition, timeout=90):
    """Waits until the latest diagnostics satisfy `condition` (and stay so for a moment)."""
    deadline, since = time.time() + timeout, None
    while time.time() < deadline:
        try:
            diagnostics(inbox.get(timeout=0.5))
        except queue.Empty:
            pass
        d = latest['diagnostics']
        if d is not None and condition(d):
            since = since or time.time()
            if time.time() - since > 3:
                check(what, True)
                return
        else:
            since = None
    print('  diagnostics:', json.dumps(latest['diagnostics'], ensure_ascii=False)[:2000])
    check(what, False)


def on_line(line, source=None, message=None):
    def condition(d):
        return any(x['range']['start']['line'] == line - 1 and (source is None or x.get('source') == source)
                   and (message is None or message in x['message']) for x in d)
    return condition


text = open(path, encoding='utf-8').read()
lines = text.split('\n')
line_of = lambda needle: next(i + 1 for i, l in enumerate(lines) if needle in l)
text_line, fun_line, using_line = line_of('Text("$prefix'), line_of('fun StickerRow('), line_of('using namespace kompoz;')

legend = start()['capabilities']['semanticTokensProvider']['legend']
version = [1]


def change(range_, new_text):
    """An incremental change, as editors send them."""
    version[0] += 1
    send({'jsonrpc': '2.0', 'method': 'textDocument/didChange',
          'params': {'textDocument': {'uri': uri, 'version': version[0]}, 'contentChanges': [{'range': range_, 'text': new_text}]}})


def at(line, column):
    return {'line': line - 1, 'character': column}


def replace_in_line(line, old, new):
    column = lines[line - 1].index(old)
    change({'start': at(line, column), 'end': at(line, column + len(old))}, new)
    lines[line - 1] = lines[line - 1].replace(old, new, 1)


if not args.no_clangd:
    wait_for('the file as it is: no diagnostics', lambda d: len(d) == 0)
    replace_in_line(text_line, 'sticker.label', 'sticker.labell')
    wait_for("an error in the Kotlin's C++: on its Kotlin line", on_line(text_line, 'kompozc (C++)', 'labell'))
    replace_in_line(text_line, 'sticker.labell', 'sticker.label')
    wait_for('fixed: no diagnostics', lambda d: len(d) == 0)
    replace_in_line(using_line, 'kompoz;', 'kompozz;')
    wait_for('an error in the C++ around the Kotlin: where it is', on_line(using_line, 'clang'))
    replace_in_line(using_line, 'kompozz;', 'kompoz;')
    wait_for('fixed again: no diagnostics', lambda d: len(d) == 0)
    hover = request('textDocument/hover', {'textDocument': {'uri': uri}, 'position': at(using_line, lines[using_line - 1].index('kompoz;') + 2)})
    check("clangd's hover in the C++", hover is not None)
    hover = request('textDocument/hover', {'textDocument': {'uri': uri}, 'position': at(text_line, lines[text_line - 1].index('Text') + 1)})
    check("no clangd hover inside the Kotlin", hover is None)
    completion = request('textDocument/completion', {'textDocument': {'uri': uri}, 'position': at(text_line, 4)})
    check('no clangd completion inside the Kotlin', completion is not None and completion.get('items') == [])

# Semantic tokens: the Kotlin's kompozc's own (keywords, declarations, calls, strings, annotations), the C++'s clangd's.
def tokens():
    data, out, line, start = request('textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})['data'], {}, 0, 0
    for i in range(0, len(data), 5):
        line += data[i]
        start = data[i + 1] if data[i] else start + data[i + 1]
        mods = {legend['tokenModifiers'][b] for b in range(len(legend['tokenModifiers'])) if data[i + 4] >> b & 1}
        out[(line + 1, lines[line][start:start + data[i + 2]])] = (legend['tokenTypes'][data[i + 3]], mods)
    return out


seen = tokens()
greeting_line = line_of('val StickerGreeting')
for (line, word), (kind, mods) in [((fun_line - 1, '@Kompozable'), ('decorator', set())), ((fun_line, 'fun'), ('keyword', set())),
                                   ((fun_line, 'StickerRow'), ('function', {'declaration'})), ((fun_line, 'sticker'), ('parameter', {'declaration'})),
                                   ((text_line, 'Text'), ('function', set())), ((text_line, 'label'), ('property', set())),
                                   ((greeting_line, '"selam "'), ('string', set())), ((greeting_line, 'Shout'), ('variable', set()))]:
    got = seen.get((line, word))
    check(f'the Kotlin\'s {word} on line {line}: {kind}', got is not None and got[0] == kind and mods <= got[1])
if not args.no_clangd:
    check("the C++'s tokens are clangd's (kompoz: a namespace)", seen.get((using_line, 'kompoz'), ('',))[0] == 'namespace')
    # Links: the #include's, none on the Kotlin (where the masked view has an #include of its own).
    links = request('textDocument/documentLink', {'textDocument': {'uri': uri}}) or []
    link_lines = {l['range']['start']['line'] + 1 for l in links}
    check('the #include is a link, nothing on the Kotlin is', line_of('#include') in link_lines and line_of('data class Sticker') not in link_lines)

formatting = request('textDocument/formatting', {'textDocument': {'uri': uri}, 'options': {'tabSize': 4, 'insertSpaces': True}})
check('no formatting of a file with Kotlin', formatting is None)
replace_in_line(fun_line, 'StickerRow(', 'StickerRow((')
wait_for("kompozc's error in the Kotlin: where it is", on_line(fun_line, 'kompozc'))

check('exits after exit', stop() == 0)

# The build's commands with no -std (CMake writes none where the compiler's default will do, as GCC 16's C++20),
# from a compiler that says it defaults to C++20: kompozc gives clangd that, which would take its own, C++17.
if not args.no_clangd and os.name != 'nt':
    build = os.path.dirname(os.path.dirname(os.path.abspath(args.kompozc)))
    with tempfile.TemporaryDirectory() as tmp:
        compiler = os.path.join(tmp, 'c++')
        with open(compiler, 'w') as f:
            f.write('#!/bin/sh\necho "#define __cplusplus 202002L"\n')
        os.chmod(compiler, 0o755)
        entries = json.load(open(os.path.join(build, 'compile_commands.json')))
        for e in entries:
            arguments = e.pop('arguments', None) or shlex.split(e.pop('command'))
            e['arguments'] = [compiler] + [a for a in arguments[1:] if not a.startswith('-std=')]
        json.dump(entries, open(os.path.join(tmp, 'compile_commands.json'), 'w'))
        with open(os.path.join(build, 'kompozc_commands.json')) as f, open(os.path.join(tmp, 'kompozc_commands.json'), 'w') as g:
            g.write(f.read())
        lines = text.split('\n')
        start(['--compile-commands-dir=' + tmp])
        wait_for("no -std in the commands: the compiler's default given to clangd, no diagnostics", lambda d: len(d) == 0)
        stop()
print(f'\n{failures} failed checks')
sys.exit(1 if failures else 0)
