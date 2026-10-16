// Kompoz documentation site: search, theme, menu, copy buttons and the table of contents.
(function () {
    'use strict';
    var ui = window.KOMPOZ_UI || {};
    var root = document.documentElement;

    // ---- Theme: light, dark, or the system's when never chosen ----
    var themeButton = document.querySelector('.theme-button');
    if (themeButton) themeButton.addEventListener('click', function () {
        var dark = root.dataset.theme ? root.dataset.theme === 'dark'
                                      : matchMedia('(prefers-color-scheme: dark)').matches;
        root.dataset.theme = dark ? 'light' : 'dark';
        try { localStorage.setItem('kompoz-theme', root.dataset.theme); } catch (e) {}
    });

    // ---- Menu on narrow windows ----
    var menuButton = document.querySelector('.menu-button');
    function setMenu(open) {
        document.body.classList.toggle('menu-open', open);
        if (menuButton) menuButton.setAttribute('aria-expanded', String(open));
    }
    if (menuButton) menuButton.addEventListener('click', function () {
        setMenu(!document.body.classList.contains('menu-open'));
    });
    var scrim = document.querySelector('.scrim');
    if (scrim) scrim.addEventListener('click', function () { setMenu(false); });
    var current = document.querySelector('.side a.current');
    if (current && current.scrollIntoView) current.scrollIntoView({block: 'center'});

    // ---- Copy buttons ----
    document.querySelectorAll('.code .copy').forEach(function (button) {
        button.title = ui.copy || 'Copy';
        button.addEventListener('click', function () {
            var text = button.parentNode.querySelector('code').innerText;
            var done = function () {
                button.classList.add('done');
                button.title = ui.copied || 'Copied';
                setTimeout(function () { button.classList.remove('done'); button.title = ui.copy || 'Copy'; }, 1500);
            };
            if (navigator.clipboard) navigator.clipboard.writeText(text).then(done, function () {});
        });
    });

    // ---- Table of contents: the section being read ----
    var tocLinks = Array.prototype.slice.call(document.querySelectorAll('.toc a'));
    if (tocLinks.length && 'IntersectionObserver' in window) {
        var byId = {};
        tocLinks.forEach(function (a) { byId[decodeURIComponent(a.hash.slice(1))] = a; });
        var visible = {};
        var observer = new IntersectionObserver(function (entries) {
            entries.forEach(function (e) { visible[e.target.id] = e.isIntersecting; });
            var headings = document.querySelectorAll('article h2[id], article h3[id]');
            var active = null;
            for (var i = 0; i < headings.length; i++) {
                if (visible[headings[i].id]) { active = headings[i].id; break; }
                if (headings[i].getBoundingClientRect().top < 120) active = headings[i].id;
            }
            tocLinks.forEach(function (a) { a.classList.toggle('active', byId[active] === a); });
        }, {rootMargin: '-60px 0px -60% 0px'});
        document.querySelectorAll('article h2[id], article h3[id]').forEach(function (h) { observer.observe(h); });
    }

    // ---- Search ----
    var input = document.querySelector('.search input');
    var results = document.querySelector('.search .results');
    if (!input || !results) return;
    var lang = root.lang || 'en';

    function fold(s) {
        return s.toLocaleLowerCase(lang).normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/ı/g, 'i');
    }
    function escapeHtml(s) {
        return s.replace(/[&<>"]/g, function (c) { return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]; });
    }
    function mark(text, terms) {
        var html = escapeHtml(text);
        terms.forEach(function (t) {
            if (!t) return;
            var folded = fold(html), out = '', at = 0, i;
            while ((i = folded.indexOf(t, at)) >= 0) {
                out += html.slice(at, i) + '<mark>' + html.slice(i, i + t.length) + '</mark>';
                at = i + t.length;
            }
            html = out + html.slice(at);
        });
        return html;
    }

    var index = null;
    function entries() {
        if (!index) index = (window.KOMPOZ_SEARCH || []).map(function (e) {
            return {e: e, p: fold(e.p), h: fold(e.h), x: fold(e.x)};
        });
        return index;
    }

    var selected = -1;
    function search() {
        var q = fold(input.value.trim());
        if (!q) { results.classList.remove('open'); return; }
        var terms = q.split(/\s+/);
        var found = [];
        entries().forEach(function (item) {
            var score = 0;
            for (var k = 0; k < terms.length; k++) {
                var t = terms[k], s = 0;
                if (item.p.indexOf(t) >= 0) s += item.h ? 4 : 12;
                if (item.h.indexOf(t) >= 0) s += 8;
                if (item.x.indexOf(t) >= 0) s += 1;
                if (!s) return;
                score += s;
            }
            if (item.h === '' && item.p === q) score += 20;
            found.push({item: item, score: score});
        });
        found.sort(function (a, b) { return b.score - a.score; });
        found = found.slice(0, 12);
        selected = found.length ? 0 : -1;
        if (!found.length) {
            results.innerHTML = '<div class="empty">' + escapeHtml(ui.noresults || 'No results') + '</div>';
        } else {
            results.innerHTML = found.map(function (f, i) {
                var e = f.item.e, snippet = '';
                var at = f.item.x.indexOf(terms[0]);
                if (at >= 0) snippet = (at > 40 ? '…' : '') + e.x.slice(Math.max(0, at - 40), at + 120);
                return '<a href="' + e.u + '"' + (i === 0 ? ' class="active"' : '') + '>' +
                    '<span class="where">' + escapeHtml(e.p) + '</span>' +
                    '<span class="what">' + mark(e.h || e.p, terms) + '</span>' +
                    (snippet ? '<span class="snippet">' + mark(snippet, terms) + '</span>' : '') + '</a>';
            }).join('');
        }
        results.classList.add('open');
    }

    function move(delta) {
        var links = results.querySelectorAll('a');
        if (!links.length) return;
        selected = (selected + delta + links.length) % links.length;
        links.forEach(function (a, i) { a.classList.toggle('active', i === selected); });
        links[selected].scrollIntoView({block: 'nearest'});
    }

    input.addEventListener('input', search);
    input.addEventListener('focus', function () { if (input.value.trim()) search(); });
    input.addEventListener('keydown', function (e) {
        if (e.key === 'ArrowDown') { move(1); e.preventDefault(); }
        else if (e.key === 'ArrowUp') { move(-1); e.preventDefault(); }
        else if (e.key === 'Enter') {
            var links = results.querySelectorAll('a');
            if (links[selected]) location.href = links[selected].href;
        } else if (e.key === 'Escape') { input.blur(); results.classList.remove('open'); }
    });
    document.addEventListener('click', function (e) {
        if (!e.target.closest('.search')) results.classList.remove('open');
    });
    document.addEventListener('keydown', function (e) {
        if (e.key === '/' && document.activeElement !== input && !/INPUT|TEXTAREA/.test(document.activeElement.tagName)) {
            e.preventDefault();
            input.focus();
        }
    });
})();
