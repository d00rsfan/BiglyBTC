#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate state/category names from the two pinned original bencode fixtures.
Usage: generate_tag_label_names.py STATE_FIXTURE CATEGORY_FIXTURE OUTPUT.cpp
"""
from pathlib import Path
import json
import sys


def rows(path):
    data = path.read_bytes()
    at = 0
    def decode():
        nonlocal at
        marker = data[at:at+1]
        at += 1
        if marker == b'i':
            end = data.index(b'e', at)
            value = int(data[at:end]); at = end + 1
            return value
        if marker in (b'l', b'd'):
            values = []
            while data[at:at+1] != b'e':
                values.append(decode())
            at += 1
            return dict(zip(values[::2], values[1::2])) if marker == b'd' else values
        at -= 1
        end = data.index(b':', at)
        count = int(data[at:end]); at = end + 1
        value = data[at:at+count]; at += count
        return value.decode('utf-8')
    result = decode()['rows']
    assert at == len(data) and len(result) == 66
    assert [r['locale'] for r in result] == sorted({r['locale'] for r in result})
    return result


def generate(states, categories):
    states = rows(states)
    categories = {r['locale']: r['names'] for r in rows(categories)}
    literal = lambda value: 'u' + json.dumps(value, ensure_ascii=False)
    lines = ['// SPDX-License-Identifier: GPL-2.0-or-later',
        '// Generated from actual pinned original MessageText fixtures.',
        '#include <biglybtc/download_state_names.hpp>', '#include <algorithm>',
        'namespace biglybtc::core::tags {', 'namespace {',
        'struct Row {std::string_view locale;DownloadStateNames names;CategoryTagNames categories;};',
        'constexpr std::array<Row,66> catalogue{{']
    for row in states:
        assert len(row['names']) == 15 and len(categories[row['locale']]) == 3
        lines.append('  {"' + row['locale'] + '",{' +
            ','.join(literal(v) for v in row['names']) + '},{' +
            ','.join(literal(v) for v in categories[row['locale']]) + '}},')
    lines += ['}};', '}',
        'std::optional<DownloadStateNames> stockDownloadStateNames(std::string_view locale)noexcept{',
        '  const auto row=std::ranges::lower_bound(catalogue,locale,{},&Row::locale);',
        '  if(row==catalogue.end()||row->locale!=locale){return {};}return row->names;', '}',
        'std::optional<CategoryTagNames> stockCategoryTagNames(std::string_view locale)noexcept{',
        '  const auto row=std::ranges::lower_bound(catalogue,locale,{},&Row::locale);',
        '  if(row==catalogue.end()||row->locale!=locale){return {};}return row->categories;', '}', '}']
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    Path(sys.argv[3]).write_text(generate(Path(sys.argv[1]), Path(sys.argv[2])))
