#!/usr/bin/env python3
import sys, re, pathlib

BASE = 3

def reindent(code: str) -> str:
    lines = code.splitlines()
    depth = 0
    out = []
    for original in lines:
        line = original.rstrip('\n')
        stripped = line.lstrip(' ')
        # Preserve completely blank lines
        if stripped == '':
            out.append('')
            continue
        # Simple state machine to count braces outside strings/template literals
        def net_braces(s: str):
            open_b = close_b = 0
            i = 0
            in_single = in_double = in_back = False
            while i < len(s):
                ch = s[i]
                if ch == '\\':
                    i += 2
                    continue
                if ch == "'" and not in_double and not in_back:
                    in_single = not in_single
                elif ch == '"' and not in_single and not in_back:
                    in_double = not in_double
                elif ch == '`' and not in_single and not in_double:
                    in_back = not in_back
                elif not (in_single or in_double or in_back):
                    if ch == '{':
                        open_b += 1
                    elif ch == '}':
                        close_b += 1
                i += 1
            return open_b, close_b
        # Adjust depth for lines starting with closing brace(s)
        temp = stripped
        leading_close = 0
        i = 0
        while i < len(temp) and temp[i] == '}':
            leading_close += 1
            i += 1
        if leading_close:
            depth = max(0, depth - leading_close)
        # Emit line with current depth
        new_line = (' ' * (depth * BASE)) + stripped
        out.append(new_line)
        # Update depth AFTER emitting based on net brace diff excluding those we already handled at start
        open_b, close_b = net_braces(stripped)
        # subtract the leading closes we already applied
        net = open_b - (close_b - leading_close)
        if net > 0:
            depth += net
        elif net < 0:  # extra unmatched closes later in line
            depth = max(0, depth + net)
    return '\n'.join(out) + '\n'

if __name__ == '__main__':
    files = sys.argv[1:]
    if not files:
        print('Usage: reindent_js.py <files...>', file=sys.stderr)
        sys.exit(1)
    for f in files:
        p = pathlib.Path(f)
        if not p.is_file():
            continue
        txt = p.read_text(encoding='utf-8')
        new = reindent(txt)
        if new != txt:
            p.write_text(new, encoding='utf-8')
