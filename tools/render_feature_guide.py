"""Render the local feature guide into standalone, browser-readable HTML."""
import html
import re
import sys
from pathlib import Path


def inline(text):
    text = html.escape(text)
    text = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", r'<img alt="\1" src="\2">', text)
    text = re.sub(r"\[([^\]]*)\]\(([^)]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r'<strong>\1</strong>', text)
    return re.sub(r"`([^`]+)`", r'<code>\1</code>', text)


def render(source):
    output, fenced, table, listing = [], False, False, False
    for line in source.splitlines():
        if line.startswith("```"):
            if table: output.append("</tbody></table>"); table = False
            if listing: output.append("</ul>"); listing = False
            output.append("</code></pre>" if fenced else "<pre><code>")
            fenced = not fenced
            continue
        if fenced:
            output.append(html.escape(line)+"\n")
            continue
        if line.startswith("|"):
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if all(re.fullmatch(r":?-+:?", cell) for cell in cells): continue
            header = not table
            if header: output.append("<table><thead>")
            tag = "th" if header else "td"
            output.append("<tr>"+"".join(f"<{tag}>{inline(cell)}</{tag}>" for cell in cells)+"</tr>")
            if header: output.append("</thead><tbody>"); table = True
            continue
        if table: output.append("</tbody></table>"); table = False
        if line.startswith("- "):
            if not listing: output.append("<ul>"); listing = True
            output.append("<li>"+inline(line[2:])+"</li>")
            continue
        if listing: output.append("</ul>"); listing = False
        match = re.match(r"^(#{1,6}) (.+)", line)
        if match:
            level = len(match[1])
            output.append(f"<h{level}>{inline(match[2])}</h{level}>")
        elif line.strip(): output.append("<p>"+inline(line)+"</p>")
    if table: output.append("</tbody></table>")
    if listing: output.append("</ul>")
    return "\n".join(output)


root = Path(__file__).resolve().parent.parent
source = root/"docs/Snipaste功能对齐-v0.4.5.md"
if len(sys.argv) > 1: source = Path(sys.argv[1]).resolve()
destination = source.with_suffix(".html")
style = """
body{margin:0;background:#101820;color:#e6edf4;font:16px/1.7 'Microsoft YaHei','Segoe UI',sans-serif}
main{max-width:1060px;margin:40px auto;padding:0 24px 70px}h1{font-size:30px}h2{margin-top:44px;color:#70ecd6}
h3{margin-top:30px}a{color:#70ecd6}table{width:100%;border-collapse:collapse;margin:20px 0;font-size:15px}
th,td{padding:10px 14px;border:1px solid #34485a;text-align:left;vertical-align:top}th{background:#234038}
tbody tr:nth-child(even){background:#172430}pre{padding:16px;background:#0c1218;overflow:auto;border-radius:8px}
code{font-family:Consolas,'Microsoft YaHei',monospace;color:#ffcf89}img{max-width:100%;border:1px solid #34485a;border-radius:8px}
strong{color:#fff}p{margin:14px 0}@media print{body{background:white;color:black}main{margin:0}th{background:#eee}a,h2,code{color:#164638}}
"""
destination.write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
                       '<title>'+html.escape(source.stem)+'</title><style>'+style+'</style><main>'+
                       render(source.read_text(encoding="utf-8"))+'</main></html>', encoding="utf-8")
print(destination)
