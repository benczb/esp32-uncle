"""Bounded TXT/EPUB conversion and durable, single-Passport reading state."""
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import textwrap
import threading
import zipfile
import xml.etree.ElementTree as ET
from html.parser import HTMLParser

MAX_UPLOAD = 4 * 1024 * 1024
MAX_TEXT = 8 * 1024 * 1024


class Text(HTMLParser):
    def __init__(self):
        super().__init__(); self.parts = []; self.hidden = 0
    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style'): self.hidden += 1
        if tag in ('p', 'div', 'br', 'h1', 'h2', 'li'): self.parts.append('\n')
    def handle_endtag(self, tag):
        if tag in ('script', 'style') and self.hidden: self.hidden -= 1
        if tag in ('p', 'div', 'h1', 'h2', 'li'): self.parts.append('\n')
    def handle_data(self, data):
        if not self.hidden: self.parts.append(data)


def convert(name, data):
    if not data or len(data) > MAX_UPLOAD: raise ValueError('File must be 1 byte to 4 MB')
    if name.lower().endswith('.txt'):
        return data.decode('utf-8-sig')
    if not name.lower().endswith('.epub'): raise ValueError('Use UTF-8 TXT or DRM-free EPUB')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        if len(infos) > 2048 or sum(x.file_size for x in infos) > 16 * 1024 * 1024:
            raise ValueError('EPUB expands beyond limit')
        container = ET.fromstring(archive.read('META-INF/container.xml'))
        rootfile = next(x for x in container.iter() if x.tag.endswith('rootfile'))
        package_name = rootfile.attrib['full-path']
        package = ET.fromstring(archive.read(package_name))
        manifest = {x.attrib['id']: x.attrib['href'] for x in package.iter() if x.tag.endswith('}item')}
        chapters = []; text_bytes = 0
        for item in package.iter():
            if not item.tag.endswith('}itemref'): continue
            href = manifest[item.attrib['idref']].split('#')[0]
            from urllib.parse import unquote
            path = str(PurePosixPath(package_name).parent / unquote(href))
            # ZIP entries are read directly, never extracted onto the filesystem.
            import posixpath
            path = posixpath.normpath(path)
            if path.startswith('../') or path.startswith('/'): raise ValueError('Invalid EPUB path')
            parser = Text(); parser.feed(archive.read(path).decode('utf-8-sig'))
            chapter = ''.join(parser.parts)
            text_bytes += len(chapter.encode()) + 2
            if text_bytes > MAX_TEXT: raise ValueError('Book text too large')
            chapters.append(chapter)
        text = '\n\n'.join(chapters)
        if len(text.encode()) > MAX_TEXT: raise ValueError('Book text too large')
        return text


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False); stream.flush(); os.fsync(stream.fileno())
    temporary.replace(path)


class Library:
    def __init__(self, root):
        self.root = Path(root); self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.lock = threading.RLock()
        try: self.state = json.loads((self.root/'reader.json').read_text())
        except (OSError, ValueError): self.state = {}
        self.reading = False
        self.cache = None

    def books(self):
        result = []
        for path in sorted(self.root.glob('*.book.json')):
            try: result.append(json.loads(path.read_text()))
            except (ValueError, OSError): continue
        return sorted(result, key=lambda b: (b['title'].casefold(), b['id']))

    def save(self): atomic_json(self.root/'reader.json', self.state)

    def add(self, name, data):
        text = convert(name, data).replace('\r\n', '\n').replace('\r', '\n')
        text = ''.join(c for c in text if c in '\n\t' or ord(c) >= 32)
        if not text.strip(): raise ValueError('Book contains no text')
        identity = hashlib.sha256(text.encode()).hexdigest()[:24]
        with self.lock:
            if len(self.books()) >= 32 and not (self.root/(identity+'.txt')).exists():
                raise ValueError('Library limit is 32 books')
            (self.root/(identity+'.txt')).write_text(text, encoding='utf-8')
            title = Path(name.replace('\\', '/')).stem[:80]
            atomic_json(self.root/(identity+'.book.json'), {'id': identity, 'title': title})
            if not self.state.get('id'): self.state['id'] = identity; self.save()
            self.cache = None
        return {'id': identity, 'title': title}

    def pages(self, identity, large):
        key = (identity, large)
        if self.cache and self.cache[0] == key: return self.cache[1]
        text = (self.root/(identity+'.txt')).read_text(encoding='utf-8')
        width, rows = (8, 6) if large else (10, 7)
        lines = []
        for paragraph in text.splitlines():
            lines.extend(textwrap.wrap(paragraph.expandtabs(2), width=width, replace_whitespace=True) or [''])
        pages = ['\n'.join(lines[i:i+rows]) for i in range(0, len(lines), rows)] or ['']
        self.cache = (key, pages)
        return pages

    def snapshot(self):
        with self.lock:
            books = self.books()
            if not books: return {'state': 'empty', 'title': 'Upload TXT or EPUB at /tools', 'text': '', 'count': 0}
            index = next((i for i,b in enumerate(books) if b['id'] == self.state.get('id')), 0)
            book = books[index]; large = bool(self.state.get('large'))
            pages = self.pages(book['id'], large)
            page = min(max(0, self.state.get('marks', {}).get(book['id'], 0)), len(pages)-1)
            return {'state': 'reading' if self.reading else 'shelf', 'title': book['title'],
                    'id': book['id'], 'index': index, 'count': len(books), 'page': page,
                    'pages': len(pages), 'large': large, 'text': pages[page] if self.reading else ''}

    def select(self, identity):
        # Validate against the shelf before constructing any file path.
        with self.lock:
            if not isinstance(identity, str) or not any(b['id'] == identity for b in self.books()):
                raise ValueError('Book not found; refresh your bookshelf')
            self.state['id'] = identity
            self.reading = True
            self.save()
            return self.snapshot()

    def control(self, action):
        if action not in ('previous', 'next', 'open', 'size', 'shelf'): raise ValueError('Unknown reader action')
        with self.lock:
            view = self.snapshot()
            if not view['count']: return view
            books = self.books(); identity = view['id']; self.state['id'] = identity
            if action == 'shelf': self.reading = False
            elif action == 'open': self.reading = not self.reading
            elif action == 'size':
                old_chars = (48 if view['large'] else 70) * view['page']
                self.state['large'] = not view['large']
                self.state.setdefault('marks', {})[identity] = old_chars // (70 if view['large'] else 48)
            elif self.reading:
                self.state.setdefault('marks', {})[identity] = min(max(0, view['page'] + (-1 if action == 'previous' else 1)), view['pages']-1)
            else: self.state['id'] = books[(view['index'] + (-1 if action == 'previous' else 1)) % len(books)]['id']
            self.save()
            return self.snapshot()
