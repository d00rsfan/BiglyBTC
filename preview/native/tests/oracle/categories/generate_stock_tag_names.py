#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate immutable C++ from OriginalStockTagNames' bencoded stock fixture.
Usage: generate_stock_tag_names.py FIXTURE OUTPUT.cpp
Run the Java oracle with the pinned core classes and an isolated config path.
The locale input is tests/fixtures/download-state-label-locales.txt.
"""
from pathlib import Path
import json
import sys

def generate(fixture):
    source = fixture.read_bytes()
    at = 0
    def decode():
        nonlocal at
        kind = source[at:at+1]
        at += 1
        if kind == b'i':
            end = source.index(b'e', at)
            value = int(source[at:end]); at = end + 1
            return value
        if kind in (b'l', b'd'):
            values = []
            while source[at:at+1] != b'e':
                values.append(decode())
            at += 1
            return dict(zip(values[::2], values[1::2])) if kind == b'd' else values
        at -= 1
        end = source.index(b':', at)
        size = int(source[at:end]); at = end + 1
        value = source[at:at+size]; at += size
        return value.decode('utf-8')
    rows = decode()['rows']
    assert at == len(source) and len(rows) == 66
    assert [r['locale'] for r in rows] == sorted({r['locale'] for r in rows})
    keys = sorted(rows[0]['names'])
    assert len(keys) == 59 and all(k.startswith('tag.') for k in keys)
    literal = lambda value: 'u' + json.dumps(value, ensure_ascii=False)
    lines = ['// SPDX-License-Identifier: GPL-2.0-or-later', '// Generated from actual original MessageText; see pinned oracle and fixture.', '#include <biglybtc/stock_tag_names.hpp>', '#include <algorithm>', '#include <array>', 'namespace biglybtc::core::tags {', 'namespace {', 'constexpr std::array<std::u16string_view,59> keys{{'+','.join(map(literal, keys))+'}};', 'struct Row {std::string_view locale;std::array<std::u16string_view,59> names;};', 'constexpr std::array<Row,66> catalogue{{']
    for row in rows:
        assert sorted(row['names']) == keys
        lines.append('  {"'+row['locale']+'",{'+','.join(literal(row['names'][k]) for k in keys)+'}},')
    lines += ['}};', '}', 'std::optional<std::u16string_view> stockTagName(std::string_view locale,std::u16string_view key)noexcept{', '  const auto row=std::ranges::lower_bound(catalogue,locale,{},&Row::locale);', '  if(row==catalogue.end()||row->locale!=locale){return {};}', '  const auto found=std::ranges::lower_bound(keys,key);', '  if(found==keys.end()||*found!=key){return {};}return row->names[static_cast<std::size_t>(found-keys.begin())];', '}', '}']
    return '\n'.join(lines) + '\n'

if __name__ == '__main__':
    Path(sys.argv[2]).write_text(generate(Path(sys.argv[1])))
