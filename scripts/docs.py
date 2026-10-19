#!/usr/bin/env python3
"""The documentation site: docs/guide/<lang>/*.md into static HTML, with no dependencies.

  scripts/docs.py [out]              builds the site into out (default build-docs/); open out/index.html
  scripts/docs.py --check [page.md]  compiles every C++ snippet (all pages, or the ones named)

Pages are Markdown (the subset below), listed in docs/guide/nav.txt. A ```cpp block is compiled on
its own after `#include <kompoz/kompoz.hpp>`, `<kompoz/testing.hpp>` and `using namespace kompoz;`:
  ```cpp            at namespace scope (functions, types)
  ```cpp body       inside a function: statements, as in a composable's body
  ```cpp continued  appended to the block before it (and that one's), as one translation unit
  ```cpp nocheck    shown, not compiled (a sketch, or what not to do)
Lines starting with "#! " are compiled but not shown (setup a snippet needs, as a declaration); in a
`cpp body` block the ones it starts with go before the function, at namespace scope.

Markdown: # ## ### headings, paragraphs, - and 1. lists (nested by indenting), ``` fences, tables,
> quotes (> **Note:** / **Warning:** become callouts), ![alt](image) (a line of pictures alone: side by
side), [text](page.md#anchor), `code`, **bold**, *italic*.
"""
import concurrent.futures
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDE = os.path.join(ROOT, 'docs', 'guide')
LANGS = ['en', 'tr']
PRELUDE = '#include <kompoz/kompoz.hpp>\n#include <kompoz/testing.hpp>\nusing namespace kompoz;\n'


def read_nav():
    """[(section titles {lang: title}, [slug, ...])]"""
    sections = []
    for line in open(os.path.join(GUIDE, 'nav.txt'), encoding='utf-8'):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line[0].isspace():
            sections[-1][1].append(line.strip())
        else:
            titles = [t.strip() for t in line.split('|')]
            sections.append((dict(zip(LANGS, titles)), []))
    return sections


def page_source(lang, slug):
    if slug == 'api':
        return os.path.join(ROOT, 'docs', 'components.md' if lang == 'en' else f'components.{lang}.md')
    return os.path.join(GUIDE, lang, slug + '.md')


# ---- Snippets --------------------------------------------------------------------------------

def code_blocks(text):
    """(line, info, code lines) of each fenced block."""
    blocks, lines, i = [], text.split('\n'), 0
    while i < len(lines):
        m = re.match(r'^(\s*)```(.*)$', lines[i])
        if m:
            indent, info, start, body = len(m.group(1)), m.group(2).strip(), i + 1, []
            i += 1
            while i < len(lines) and not re.match(r'^\s*```\s*$', lines[i]):
                body.append(lines[i][indent:] if lines[i][:indent].isspace() else lines[i].lstrip())
                i += 1
            blocks.append((start, info, body))
        i += 1
    return blocks


def snippet_units(path):
    """The translation units a page's C++ blocks make: (first line, source)."""
    units = []
    for line, info, body in code_blocks(open(path, encoding='utf-8').read()):
        words = info.split()
        if not words or words[0] != 'cpp' or 'nocheck' in words:
            continue
        where = f'#line {line + 1} "{os.path.relpath(path, ROOT)}"\n'
        if 'body' in words:  # the hidden lines it starts with are declarations, outside the function
            lead = 0
            while lead < len(body) and body[lead].startswith('#! '):
                lead += 1
            setup = '\n'.join(l[3:] for l in body[:lead])
            code = '\n'.join(l[3:] if l.startswith('#! ') else l for l in body[lead:])
            part = f'{where}{setup}\nvoid kompoz_snippet_{line}() {{\n#line {line + 1 + lead} "{os.path.relpath(path, ROOT)}"\n{code}\n}}\n'
        else:
            part = where + '\n'.join(l[3:] if l.startswith('#! ') else l for l in body) + '\n'
        if 'continued' in words and units:
            units[-1] = (units[-1][0], units[-1][1] + part)
        else:
            units.append((line, PRELUDE + part))
    return units


def check(paths):
    compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
    jobs = []
    for path in paths:
        for line, source in snippet_units(path):
            jobs.append((path, line, source))
    tmp = tempfile.mkdtemp(prefix='kompoz-docs-')

    def compile_one(job):
        path, line, source = job
        name = os.path.join(tmp, f'{os.path.basename(path)}-{line}.cpp')
        with open(name, 'w', encoding='utf-8') as f:
            f.write(source)
        r = subprocess.run([compiler, '-std=c++20', '-fsyntax-only', '-I', os.path.join(ROOT, 'include'), name],
                           capture_output=True, text=True)
        return job, r.returncode, r.stderr

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get('DOCS_JOBS', min(6, os.cpu_count() or 4)))) as pool:
        for (path, line, _), code, err in pool.map(compile_one, jobs):
            if code != 0:
                failed += 1
                print(f'--- {os.path.relpath(path, ROOT)}:{line}: does not compile\n{err}')
    shutil.rmtree(tmp, ignore_errors=True)
    print(f'{len(jobs)} snippets, {failed} failed')
    return failed == 0


def all_pages():
    paths = []
    for _, slugs in read_nav():
        for slug in slugs:
            for lang in LANGS:
                p = page_source(lang, slug)
                if os.path.exists(p):
                    paths.append(p)
    return paths


# ---- Markdown --------------------------------------------------------------------------------

def slugify(text):
    text = re.sub(r'<[^>]+>', '', text)
    text = html.unescape(text).lower().replace('İ'.lower(), 'i')
    text = re.sub(r'[^\w\s-]', '', text)
    return re.sub(r'\s+', '-', text.strip())


CPP_KEYWORDS = set('''alignas alignof auto bool break case catch char char8_t class co_await co_return co_yield
concept const consteval constexpr constinit continue decltype default delete do double else enum explicit
export extern false float for friend if inline int long mutable namespace new noexcept nullptr operator
private protected public requires return short signed sizeof static static_assert struct switch template
this throw true try typedef typename union unsigned using virtual void volatile while size_t int64_t
uint8_t uint32_t int32_t
fun val var when is in object data sealed interface override suspend by it null'''.split())
SHELL_KEYWORDS = set('if then else fi for do done while case esac function export local return in'.split())

TOKEN = re.compile(r'''(?P<com>//[^\n]*|/\*.*?\*/)|(?P<str>R"\((?:.|\n)*?\)"|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])')'''
                   r'''|(?P<pre>^[ \t]*\#[ \t]*\w+)|(?P<num>\b\d[\d'.]*(?:[eE][-+]?\d+)?[fFuUlL]*\b)|(?P<id>[A-Za-z_]\w*)''',
                   re.S | re.M)
SHELL_TOKEN = re.compile(r'''(?P<com>(?:^|(?<=\s))\#[^\n]*)|(?P<str>"(?:\\.|[^"\\])*"|'[^']*')|(?P<var>\$\{?\w+\}?)'''
                         r'''|(?P<id>[A-Za-z_][\w-]*)''', re.M)


def highlight(code, lang):
    if lang in ('cpp', 'c++', 'kotlin', 'kt', 'cmake'):
        regex, keywords = TOKEN, CPP_KEYWORDS if lang != 'cmake' else set()
    elif lang in ('bash', 'sh', 'shell', 'console'):
        regex, keywords = SHELL_TOKEN, SHELL_KEYWORDS
    else:
        return html.escape(code)
    out, pos = [], 0
    for m in regex.finditer(code):
        out.append(html.escape(code[pos:m.start()]))
        kind, text = m.lastgroup, m.group(0)
        if kind == 'id':
            after = code[m.end():m.end() + 1]
            if text in keywords:
                kind = 'kw'
            elif lang == 'cmake' and after == '(':
                kind = 'fn'
            elif lang in ('bash', 'sh', 'shell', 'console'):
                kind = None
            elif text[0].isupper():
                kind = 'fn' if after in ('(', '{') else 'ty'
            elif after == '(':
                kind = 'call'
            else:
                kind = None
        elif kind == 'com' and lang == 'cmake':
            kind = 'com'
        out.append(f'<span class="{kind}">{html.escape(text)}</span>' if kind else html.escape(text))
        pos = m.end()
    out.append(html.escape(code[pos:]))
    return ''.join(out)


class Page:
    def __init__(self, lang, slug, path, site):
        self.lang, self.slug, self.path, self.site = lang, slug, path, site
        self.title, self.summary, self.toc, self.ids, self.links = slug, '', [], set(), []

    # Inline: code spans and escapes are set aside first, so nothing inside them is touched.
    def inline(self, text):
        saved = []

        def keep(fragment):
            saved.append(fragment)
            return f'\x00{len(saved) - 1}\x00'

        text = re.sub(r'(`+)(.+?)\1', lambda m: keep(f'<code>{html.escape(m.group(2).strip())}</code>'), text)
        text = re.sub(r'\\([\\`*_\[\]|#<>!])', lambda m: keep(html.escape(m.group(1))), text)
        text = html.escape(text, quote=False)
        text = re.sub(r'!\[([^\]]*)\]\(([^)\s]+)\)', lambda m: keep(self.image(m.group(2), m.group(1))), text)
        text = re.sub(r'\[([^\]]+)\]\(([^)\s]+)\)', lambda m: self.link(m.group(2), m.group(1)), text)
        text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
        text = re.sub(r'(?<![\w*])\*(?![\s*])(.+?)(?<![\s*])\*(?![\w*])', r'<em>\1</em>', text)
        for _ in range(3):  # code inside a link's text
            text = re.sub(r'\x00(\d+)\x00', lambda m: saved[int(m.group(1))], text)
        return text

    def link(self, url, label):
        url = html.unescape(url)
        if re.match(r'^[a-z]+:', url):
            return f'<a href="{html.escape(url)}" rel="noopener">{label}</a>'
        target, _, anchor = url.partition('#')
        if not target:
            self.links.append((self.slug, anchor))
            return f'<a href="#{html.escape(anchor)}">{label}</a>'
        name = os.path.basename(target)
        if name.endswith('.md'):
            slug = name[:-3]
            slug = {'components': 'api', f'components.{self.lang}': 'api', 'README': 'index',
                    f'README.{self.lang}': 'index'}.get(slug, slug)
            if slug in self.site.slugs:
                self.links.append((slug, anchor))
                return f'<a href="{slug}.html{"#" + html.escape(anchor) if anchor else ""}">{label}</a>'
        self.site.warn(f'{os.path.relpath(self.path, ROOT)}: link to {url} left as text')
        return label

    def image(self, src, alt):
        file = os.path.normpath(os.path.join(os.path.dirname(self.path), src))
        if not os.path.exists(file):
            self.site.warn(f'{os.path.relpath(self.path, ROOT)}: no image {src}')
            return html.escape(alt)
        self.site.images.add(file)
        return f'<img src="../img/{html.escape(os.path.basename(file))}" alt="{html.escape(alt)}" loading="lazy">'

    def heading(self, level, text):
        rendered = self.inline(text)
        if level == 1:
            self.title = re.sub(r'<[^>]+>', '', rendered)
            return ''
        base = slugify(rendered) or 'section'
        hid, n = base, 2
        while hid in self.ids:
            hid, n = f'{base}-{n}', n + 1
        self.ids.add(hid)
        if level in (2, 3):
            self.toc.append((level, hid, re.sub(r'<[^>]+>', '', rendered)))
        return f'<h{level} id="{hid}"><a class="anchor" href="#{hid}" aria-hidden="true">#</a>{rendered}</h{level}>'

    def code(self, info, body):
        words = info.split()
        lang = words[0] if words else ''
        shown = [l for l in body if not l.startswith('#! ')]
        while shown and not shown[-1].strip():
            shown.pop()
        label = {'cpp': 'C++', 'kotlin': 'Kotlin', 'bash': 'Shell', 'cmake': 'CMake', 'sh': 'Shell'}.get(lang, lang)
        return (f'<div class="code" data-lang="{html.escape(label)}"><button class="copy" type="button" '
                f'aria-label="Copy"></button><pre><code>{highlight(chr(10).join(shown), lang)}</code></pre></div>')

    def table(self, rows):
        def cells(row):
            row = row.strip()
            row = row[1:] if row.startswith('|') else row
            row = row[:-1] if row.endswith('|') and not row.endswith('\\|') else row
            out, cur, tick, i = [], '', False, 0
            while i < len(row):
                c = row[i]
                if c == '\\' and i + 1 < len(row):
                    cur += row[i:i + 2]
                    i += 2
                    continue
                if c == '`':
                    tick = not tick
                if c == '|' and not tick:
                    out.append(cur)
                    cur = ''
                else:
                    cur += c
                i += 1
            out.append(cur)
            return [x.strip() for x in out]

        head, aligns = cells(rows[0]), cells(rows[1])
        style = ['right' if a.endswith(':') and not a.startswith(':') else 'center' if a.startswith(':') and a.endswith(':')
                 else '' for a in aligns]

        def row_html(cols, tag):
            return '<tr>' + ''.join(f'<{tag}{f" style=text-align:{style[i]}" if i < len(style) and style[i] else ""}>'
                                    f'{self.inline(c)}</{tag}>' for i, c in enumerate(cols)) + '</tr>'

        body = ''.join(row_html(cells(r), 'td') for r in rows[2:])
        return f'<div class="table"><table><thead>{row_html(head, "th")}</thead><tbody>{body}</tbody></table></div>'

    def blocks(self, lines):
        out, i, n = [], 0, len(lines)
        list_item = re.compile(r'^(\s*)([-*]|\d+[.)])\s+(.*)$')

        def starts_block(line):
            return (re.match(r'^\s*```', line) or re.match(r'^#{1,6}\s', line) or line.lstrip().startswith('>')
                    or line.lstrip().startswith('|') or list_item.match(line))

        while i < n:
            line = lines[i]
            if not line.strip():
                i += 1
                continue
            m = re.match(r'^(\s*)```(.*)$', line)
            if m:
                indent, info, body = len(m.group(1)), m.group(2).strip(), []
                i += 1
                while i < n and not re.match(r'^\s*```\s*$', lines[i]):
                    body.append(lines[i][indent:] if lines[i][:indent].isspace() else lines[i].lstrip())
                    i += 1
                i += 1
                out.append(self.code(info, body))
                continue
            m = re.match(r'^(#{1,6})\s+(.*?)\s*#*\s*$', line)
            if m:
                out.append(self.heading(len(m.group(1)), m.group(2)))
                i += 1
                continue
            if line.lstrip().startswith('|') and i + 1 < n and re.match(r'^\s*\|?\s*:?-{2,}', lines[i + 1]):
                rows = []
                while i < n and lines[i].lstrip().startswith('|'):
                    rows.append(lines[i])
                    i += 1
                out.append(self.table(rows))
                continue
            if line.lstrip().startswith('>'):
                quoted = []
                while i < n and lines[i].lstrip().startswith('>'):
                    quoted.append(re.sub(r'^\s*>\s?', '', lines[i]))
                    i += 1
                first = quoted[0] if quoted else ''
                kind = ('warning' if re.match(r'\*\*(Warning|Uyarı|Dikkat)', first)
                        else 'tip' if re.match(r'\*\*(Tip|İpucu)', first)
                        else 'note' if re.match(r'\*\*(Note|Not)\b', first) else 'quote')
                out.append(f'<div class="callout {kind}">{self.blocks(quoted)}</div>')
                continue
            m = list_item.match(line)
            if m:
                i = self.list_block(lines, i, out)
                continue
            para = [line.strip()]
            i += 1
            while i < n and lines[i].strip() and not starts_block(lines[i]):
                para.append(lines[i].strip())
                i += 1
            text = ' '.join(para)
            images = re.findall(r'!\[[^\]]*\]\([^)]+\)', text)
            if images and re.fullmatch(r'(?:!\[[^\]]*\]\([^)]+\)\s*)+', text):  # pictures alone: side by side
                row = ' class="row"' if len(images) > 1 else ''
                out.append(f'<figure{row}>{self.inline(text)}</figure>')
            else:
                rendered = self.inline(text)
                if not self.summary and not out[-1:] == [''] and self.title != self.slug:
                    self.summary = re.sub(r'<[^>]+>', '', rendered)
                out.append(f'<p>{rendered}</p>')
        return '\n'.join(out)

    def list_block(self, lines, i, out):
        item_re = re.compile(r'^(\s*)([-*]|\d+[.)])\s+(.*)$')
        first = item_re.match(lines[i])
        base = len(first.group(1))
        ordered = first.group(2)[0].isdigit()
        start = int(re.match(r'\d+', first.group(2)).group(0)) if ordered else 1
        items, n = [], len(lines)
        while i < n:
            line = lines[i]
            m = item_re.match(line)
            if m and len(m.group(1)) == base and m.group(2)[0].isdigit() == ordered:
                content = len(m.group(1)) + len(m.group(2)) + 1
                items.append([m.group(3)])
                i += 1
                continue
            if not line.strip():
                j = i + 1
                while j < n and not lines[j].strip():
                    j += 1
                if j < n and (len(lines[j]) - len(lines[j].lstrip()) > base or
                              (item_re.match(lines[j]) and len(item_re.match(lines[j]).group(1)) == base)):
                    items[-1].append('')
                    i += 1
                    continue
                break
            indent = len(line) - len(line.lstrip())
            if indent > base:
                items[-1].append(line[min(indent, content):])
                i += 1
                continue
            if items[-1][-1].strip() and not item_re.match(line) and not re.match(r'^(#|```|\||>)', line.lstrip()):
                items[-1].append(line.strip())  # a lazy continuation of the item's text
                i += 1
                continue
            break
        html_items = []
        for item in items:
            inner = self.blocks(item)
            if inner.count('<p>') == 1 and inner.startswith('<p>'):
                inner = inner.replace('<p>', '', 1).replace('</p>', '', 1)
            html_items.append(f'<li>{inner}</li>')
        tag = 'ol' if ordered else 'ul'
        attr = f' start="{start}"' if ordered and start != 1 else ''
        out.append(f'<{tag}{attr}>{"".join(html_items)}</{tag}>')
        return i

    def render(self):
        text = open(self.path, encoding='utf-8').read()
        lines = [l for l in text.split('\n')
                 if not re.match(r'^\s*(\*\*[^*]+\*\* · \[[^\]]+\]\([^)]+\)|\[[^\]]+\]\([^)]+\) · \*\*[^*]+\*\*)\s*$', l)]
        self.body = self.blocks(lines)
        return self


def plain(fragment):
    return re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', ' ', fragment))).strip()


# ---- The site --------------------------------------------------------------------------------

UI = {
    'en': {'search': 'Search the docs', 'toc': 'On this page', 'prev': 'Previous', 'next': 'Next',
           'other': 'Türkçe', 'menu': 'Menu', 'theme': 'Switch theme', 'noresults': 'No results',
           'footer': 'Kompoz is licensed under the Apache License 2.0. Code samples on this site are part of it.',
           'copy': 'Copy', 'copied': 'Copied'},
    'tr': {'search': 'Belgelerde ara', 'toc': 'Bu sayfada', 'prev': 'Önceki', 'next': 'Sonraki',
           'other': 'English', 'menu': 'Menü', 'theme': 'Temayı değiştir', 'noresults': 'Sonuç yok',
           'footer': 'Kompoz, Apache Lisansı 2.0 ile lisanslıdır. Bu sitedeki kod örnekleri de onun parçasıdır.',
           'copy': 'Kopyala', 'copied': 'Kopyalandı'},
}

LOGO = ('<svg class="logo" viewBox="0 0 32 32" aria-hidden="true"><rect x="3" y="3" width="26" height="26" rx="8" '
        'fill="var(--primary)"/><path d="M11 9v14M11 16l8-7M13.5 14l6 9" stroke="var(--on-primary)" stroke-width="3" '
        'stroke-linecap="round" stroke-linejoin="round" fill="none"/></svg>')


class Site:
    def __init__(self, out):
        self.out, self.warnings, self.images = out, [], set()
        self.nav = read_nav()
        self.slugs = {s for _, slugs in self.nav for s in slugs}
        m = re.search(r'project\(kompoz VERSION ([\d.]+)', open(os.path.join(ROOT, 'CMakeLists.txt')).read())
        self.version = m.group(1) if m else ''

    def warn(self, message):
        self.warnings.append(message)

    def build(self):
        if os.path.isdir(self.out):
            shutil.rmtree(self.out)
        os.makedirs(os.path.join(self.out, 'assets'))
        pages = {}
        for lang in LANGS:
            for _, slugs in self.nav:
                for slug in slugs:
                    path = page_source(lang, slug)
                    if os.path.exists(path):
                        pages[lang, slug] = Page(lang, slug, path, self).render()
                    else:
                        self.warn(f'missing page {os.path.relpath(path, ROOT)}')
        for page in pages.values():
            for slug, anchor in page.links:
                target = pages.get((page.lang, slug))
                if anchor and target and anchor not in target.ids:
                    self.warn(f'{os.path.relpath(page.path, ROOT)}: no #{anchor} in {slug}')
        for lang in LANGS:
            order = [pages[lang, s] for _, slugs in self.nav for s in slugs if (lang, s) in pages]
            os.makedirs(os.path.join(self.out, lang), exist_ok=True)
            for k, page in enumerate(order):
                prev = order[k - 1] if k > 0 else None
                nxt = order[k + 1] if k + 1 < len(order) else None
                with open(os.path.join(self.out, lang, page.slug + '.html'), 'w', encoding='utf-8') as f:
                    f.write(self.page_html(page, pages, prev, nxt))
            with open(os.path.join(self.out, 'assets', f'search-{lang}.js'), 'w', encoding='utf-8') as f:
                f.write('window.KOMPOZ_SEARCH=' + json.dumps(self.search_index(order), ensure_ascii=False,
                                                              separators=(',', ':')) + ';\n')
        if self.images:
            os.makedirs(os.path.join(self.out, 'img'))
            for image in self.images:
                shutil.copy(image, os.path.join(self.out, 'img'))
        for name in ('site.css', 'site.js'):
            shutil.copy(os.path.join(GUIDE, 'assets', name), os.path.join(self.out, 'assets', name))
        with open(os.path.join(self.out, 'index.html'), 'w', encoding='utf-8') as f:
            f.write(ROOT_INDEX)
        for w in self.warnings:
            print('warning:', w)
        print(f'{len(pages)} pages into {os.path.relpath(self.out, os.getcwd())}/, {len(self.warnings)} warnings')

    def search_index(self, order):
        entries = []
        for page in order:
            parts = re.split(r'(<h[23] id="[^"]+">.*?</h[23]>)', page.body)
            heading, anchor = '', ''
            for part in parts:
                m = re.match(r'<h[23] id="([^"]+)">(.*?)</h[23]>', part)
                if m:
                    anchor, heading = m.group(1), plain(m.group(2)).lstrip('#')
                    continue
                url = page.slug + '.html' + ('#' + anchor if anchor else '')
                rows = re.findall(r'<tr><td>(.*?)</td>(.*?)</tr>', part, re.S)
                part = re.sub(r'<div class="table">.*?</div>', ' ', part, flags=re.S)
                text = plain(part)
                if text or heading:
                    entries.append({'p': page.title, 'h': heading, 'u': url, 'x': text[:2000]})
                for first, rest in rows:  # a table's rows on their own: the API page's entries
                    entries.append({'p': page.title + (' › ' + heading if heading else ''), 'h': plain(first),
                                    'u': url, 'x': plain(rest)[:600]})
        return entries

    def page_html(self, page, pages, prev, nxt):
        ui, lang = UI[page.lang], page.lang
        other = LANGS[1 - LANGS.index(lang)]
        other_slug = page.slug if (other, page.slug) in pages else 'index'
        section_title = ''
        nav = []
        for titles, slugs in self.nav:
            links = []
            for s in slugs:
                p = pages.get((lang, s))
                if not p:
                    continue
                current = s == page.slug
                if current:
                    section_title = titles.get(lang, titles['en'])
                links.append(f'<a href="{s}.html"{" class=current aria-current=page" if current else ""}>'
                             f'{html.escape(p.title)}</a>')
            if links:
                is_open = any(s == page.slug for s in slugs) or True
                nav.append(f'<details{" open" if is_open else ""}><summary>{html.escape(titles.get(lang, titles["en"]))}'
                           f'</summary>{"".join(links)}</details>')
        toc = ''.join(f'<a class="l{level}" href="#{hid}">{html.escape(text)}</a>' for level, hid, text in page.toc)
        pager = ''
        if prev:
            pager += f'<a class="prev" href="{prev.slug}.html"><span>{ui["prev"]}</span>{html.escape(prev.title)}</a>'
        if nxt:
            pager += f'<a class="next" href="{nxt.slug}.html"><span>{ui["next"]}</span>{html.escape(nxt.title)}</a>'
        title = 'Kompoz' if page.slug == 'index' else f'{page.title} · Kompoz'
        return f'''<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<meta name="description" content="{html.escape(page.summary)}">
<link rel="stylesheet" href="../assets/site.css">
<script>try{{var t=localStorage.getItem('kompoz-theme');if(t)document.documentElement.dataset.theme=t}}catch(e){{}}</script>
</head>
<body class="page-{page.slug}">
<header class="top">
<button class="menu-button" type="button" aria-label="{ui['menu']}" aria-expanded="false"><span></span></button>
<a class="brand" href="index.html">{LOGO}<span>Kompoz</span></a><span class="version">{self.version}</span>
<div class="search"><input type="search" placeholder="{ui['search']}" aria-label="{ui['search']}" autocomplete="off" spellcheck="false"><kbd>/</kbd><div class="results" role="listbox"></div></div>
<a class="language" href="../{other}/{other_slug}.html" hreflang="{other}" lang="{other}">{ui['other']}</a>
<button class="theme-button" type="button" aria-label="{ui['theme']}" title="{ui['theme']}"></button>
</header>
<div class="layout">
<nav class="side" aria-label="{ui['menu']}">{"".join(nav)}</nav>
<main>
<div class="crumbs">{html.escape(section_title)}</div>
<article>
<h1>{html.escape(page.title)}</h1>
{page.body}
</article>
<nav class="pager">{pager}</nav>
<footer>{ui['footer']}</footer>
</main>
<aside class="toc">{f'<div class="toc-title">{ui["toc"]}</div>{toc}' if toc else ''}</aside>
</div>
<div class="scrim"></div>
<script>window.KOMPOZ_UI={json.dumps({k: ui[k] for k in ('noresults', 'copy', 'copied')}, ensure_ascii=False)};</script>
<script src="../assets/search-{lang}.js" defer></script>
<script src="../assets/site.js" defer></script>
</body>
</html>
'''


ROOT_INDEX = '''<!doctype html>
<html><head><meta charset="utf-8"><title>Kompoz</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<script>
var l = (navigator.languages && navigator.languages[0]) || navigator.language || 'en';
location.replace((/^tr\\b/i.test(l) ? 'tr' : 'en') + '/index.html');
</script></head>
<body><p><a href="en/index.html">Kompoz documentation</a> · <a href="tr/index.html">Kompoz belgeleri</a></p></body></html>
'''


if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == '--check':
        sys.exit(0 if check([os.path.abspath(p) for p in args[1:]] or all_pages()) else 1)
    Site(os.path.abspath(args[0] if args else os.path.join(ROOT, 'build-docs'))).build()
