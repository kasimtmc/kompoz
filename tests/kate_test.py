#!/usr/bin/env python3
"""Kate's Kompoz C++ (compiler/kate/kompoz-cpp.xml) and kompozc --kate.

    python3 tests/kate_test.py --kompozc build/compiler/kompozc

The definition, as KSyntaxHighlighting reads it (its command line highlighter: ksyntaxhighlighter6, or KF5's
kate-syntax-highlighter), on kate_sample.cpp: its C++ lines highlighted as Kate's C++ highlights them alone, and
the Kotlin's tokens in the styles below (read back from a theme that gives each style a color of its own).
kompozc --kate: the definition written where Kate reads it, and kompozc's server added to Kate's settings, their
other servers kept. Exits 1 on a failed check."""

import argparse, html, json, os, re, shutil, subprocess, sys, tempfile

ap = argparse.ArgumentParser()
ap.add_argument('--kompozc', required=True)
args = ap.parse_args()
here = os.path.dirname(os.path.abspath(__file__))
definition = os.path.join(os.path.dirname(here), 'compiler', 'kate', 'kompoz-cpp.xml')
tool = shutil.which('ksyntaxhighlighter6') or shutil.which('kate-syntax-highlighter')
if not tool:
    sys.exit('no ksyntaxhighlighter6 or kate-syntax-highlighter')
lines = open(os.path.join(here, 'kate_sample.cpp'), encoding='utf-8').read().split('\n')
kotlin = set(range(16, 36))  # the Kotlin's lines (1-based): from the data class to the sealed class

# Tokens of the Kotlin: (line, text, style); a text found in that line, its first time.
expected = [
    (16, 'data', 'Keyword'), (16, 'class', 'Keyword'), (16, 'Item', 'DataType'), (16, 'val', 'Keyword'),
    (16, 'id', 'Variable'), (16, 'Int', 'DataType'), (16, 'false', 'Constant'),
    (18, '@Kompozable', 'Attribute'),
    (19, 'fun', 'Keyword'), (19, 'ItemRow', 'Function'), (19, 'Boolean', 'DataType'), (19, '->', 'Operator'),
    (20, 'Row', 'Function'), (20, 'fillMaxWidth', 'Function'), (20, '!', 'Operator'), (20, '{', 'Operator'),
    (21, 'Checkbox', 'Function'),
    (22, '// a comment in the Kotlin', 'Comment'),
    (23, '"', 'String'), (23, '${', 'SpecialChar'), (23, '$item', 'SpecialChar'), (23, ' #', 'String'),
    (23, 'MaterialTheme', 'DataType'),
    (26, '// a comment at a line', 'Comment'),
    (27, 'private', 'Keyword'), (27, 'Spacing', 'Variable'), (27, '8', 'DecVal'),
    (29, 'object', 'Keyword'), (29, 'Routes', 'DataType'),
    (30, 'const', 'Keyword'), (30, 'Home', 'Variable'), (30, '"home"', 'String'),
    (33, 'enum', 'Keyword'), (33, 'Mode', 'DataType'), (33, 'Light', 'Function'),
    (35, 'sealed', 'Keyword'), (35, 'Screen', 'DataType'), (35, 'route', 'Variable'),
]
styles = ['Normal', 'Keyword', 'Function', 'Variable', 'ControlFlow', 'Operator', 'BuiltIn', 'Extension',
          'Preprocessor', 'Attribute', 'Char', 'SpecialChar', 'String', 'VerbatimString', 'SpecialString', 'Import',
          'DataType', 'DecVal', 'BaseN', 'Float', 'Constant', 'Comment', 'Documentation', 'Annotation',
          'CommentVar', 'RegionMarker', 'Information', 'Warning', 'Alert', 'Others', 'Error']
color = {s: '#%06x' % (i + 1) for i, s in enumerate(styles)}
style_of = {c: s for s, c in color.items()}


def highlighted(syntax, text, data_home):
    """Each line as (style, text) pieces."""
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False, encoding='utf-8') as f:
        f.write(text)
    try:
        out = subprocess.run([tool, '-s', syntax, '-t', 'Kompoz Test', '-f', 'html', f.name], capture_output=True,
                             text=True, check=True, env=dict(os.environ, XDG_DATA_HOME=data_home)).stdout
    finally:
        os.unlink(f.name)
    result = []
    for line in re.search(r'<pre>\n?(.*)</pre>', out, re.S).group(1).split('\n'):
        pieces, at = [], 0
        for m in re.finditer(r'<span style="([^"]*)">(.*?)</span>', line):
            if m.start() > at:
                pieces.append(('Normal', html.unescape(line[at:m.start()])))
            c = re.search(r'color:(#[0-9a-f]{6})', m.group(1))
            pieces.append((style_of.get(c.group(1), '?') if c else 'Normal', html.unescape(m.group(2))))
            at = m.end()
        if at < len(line):
            pieces.append(('Normal', html.unescape(line[at:])))
        result.append(pieces)
    return result


with tempfile.TemporaryDirectory() as data_home:
    base = os.path.join(data_home, 'org.kde.syntax-highlighting')
    os.makedirs(os.path.join(base, 'syntax'))
    os.makedirs(os.path.join(base, 'themes'))
    shutil.copy(definition, os.path.join(base, 'syntax'))
    with open(os.path.join(base, 'themes', 'kompoz-test.theme'), 'w') as f:
        json.dump({'metadata': {'name': 'Kompoz Test', 'revision': 1},
                   'text-styles': {s: {'text-color': c} for s, c in color.items()},
                   'editor-colors': {'BackgroundColor': '#ffffff'}}, f)
    mine = highlighted('Kompoz C++', '\n'.join(lines), data_home)
    as_cpp = highlighted('C++', '\n'.join('' if i + 1 in kotlin else l for i, l in enumerate(lines)), data_home)

failures = 0
for i, line in enumerate(lines):
    if i + 1 not in kotlin and mine[i] != as_cpp[i]:
        failures += 1
        print(f'[FAIL] line {i + 1}, C++: {line}\n  got      {mine[i]}\n  expected {as_cpp[i]}')
for line, text, style in expected:
    got = next((s for s, t in mine[line - 1] if t.startswith(text)), None)
    if got != style:
        failures += 1
        print(f'[FAIL] line {line}, Kotlin: {text!r} is {got}, not {style}  ({mine[line - 1]})')
print(f'{len(lines)} lines, {len(expected)} Kotlin tokens: {failures} failed')


def check(what, ok):
    global failures
    print(('[ OK ] ' if ok else '[FAIL] ') + what)
    failures += not ok


# kompozc --kate, in a home of its own, whose Kate settings have a server of the user's.
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, 'data'), XDG_CONFIG_HOME=os.path.join(home, 'config'))
    settings = os.path.join(home, 'config', 'kate', 'lspclient', 'settings.json')
    os.makedirs(os.path.dirname(settings))
    mine_server = {'command': ['my-lsp'], 'highlightingModeRegex': '^Mine$'}
    json.dump({'servers': {'mine': mine_server}, 'other': 1}, open(settings, 'w'))
    done = subprocess.run([args.kompozc, '--kate'], env=env, capture_output=True, text=True)
    check('kompozc --kate: done', done.returncode == 0)
    written = os.path.join(home, 'data', 'org.kde.syntax-highlighting', 'syntax', 'kompoz-cpp.xml')
    check('the definition where Kate reads it, as it is in compiler/kate',
          os.path.exists(written) and open(written, 'rb').read() == open(definition, 'rb').read())
    config = json.load(open(settings))
    server = config.get('servers', {}).get('kompoz', {})
    check('the user\'s server and settings kept', config['servers'].get('mine') == mine_server and config.get('other') == 1)
    check('kompozc --lsp for Kompoz C++, by its path, with no language id',
          server.get('command', [None])[0] == os.path.realpath(args.kompozc) and server['command'][1:2] == ['--lsp']
          and server.get('highlightingModeRegex') == '^Kompoz C\\+\\+$' and server.get('documentLanguageId') is False)
    with open(settings, 'w') as f:
        f.write('{ not JSON')
    broken = subprocess.run([args.kompozc, '--kate'], env=env, capture_output=True, text=True)
    check('settings that aren\'t JSON: left as they are, an error', broken.returncode != 0 and open(settings).read() == '{ not JSON')

print(f'\n{failures} failed checks')
sys.exit(1 if failures else 0)
