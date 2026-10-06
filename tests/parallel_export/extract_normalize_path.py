from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
signature = "std::optional<QString> NormalizePath(const Settings &settings) {"
begin = source.index(signature)
position = source.index("{", begin)
depth = 0
end = None
for index in range(position, len(source)):
    if source[index] == "{":
        depth += 1
    elif source[index] == "}":
        depth -= 1
        if depth == 0:
            end = index + 1
            break
if end is None:
    raise RuntimeError("NormalizePath function body is incomplete")
function = source[begin:end]
Path(sys.argv[2]).write_text(
    '#include "filesystem_domain.h"\n\n'
    'namespace Export::Output {\n\n'
    + function
    + '\n\n}\n'
)
