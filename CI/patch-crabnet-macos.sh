#!/usr/bin/env bash
set -euo pipefail

CRABNET_DIR="${1:?usage: patch-crabnet-macos.sh <CrabNet source directory>}"
MEMORY_POOL_HEADER="$CRABNET_DIR/include/raknet/DS_MemoryPool.h"
TABLE_SOURCE="$CRABNET_DIR/Source/DS_Table.cpp"
RAND_SOURCE="$CRABNET_DIR/Source/Utils/Rand.cpp"

python3 - "$MEMORY_POOL_HEADER" "$TABLE_SOURCE" "$RAND_SOURCE" <<'PY'
from pathlib import Path
import re
import sys

memory_pool = Path(sys.argv[1])
table_source = Path(sys.argv[2])
rand_source = Path(sys.argv[3])

for path in (memory_pool, table_source, rand_source):
    if not path.is_file():
        raise SystemExit(f"CrabNet compatibility patch: missing {path}")

text = memory_pool.read_text(encoding="utf-8")
old = text

# Old CrabNet deliberately excluded <stdlib.h> on Apple. Modern AppleClang no
# longer exposes malloc/free transitively, so the header must include it too.
text, replacements = re.subn(
    r"#ifndef __APPLE__\s*\n"
    r"// Use stdlib and not malloc for compatibility\s*\n"
    r"#include <stdlib\.h>\s*\n"
    r"#endif",
    "// malloc/free are used directly by this header.\n#include <stdlib.h>",
    text,
    count=1,
)
if replacements == 0 and "#include <stdlib.h>" not in text:
    marker = "#define __MEMORY_POOL_H\n"
    if marker not in text:
        raise SystemExit(f"CrabNet compatibility patch: unexpected layout in {memory_pool}")
    text = text.replace(marker, marker + "\n#include <stdlib.h>\n", 1)

if text != old:
    memory_pool.write_text(text, encoding="utf-8")
    print(f"Patched {memory_pool}")
else:
    print(f"Already compatible: {memory_pool}")

text = table_source.read_text(encoding="utf-8")
if "#include <stdlib.h>" not in text:
    marker = '#include "DS_Table.h"'
    if marker not in text:
        raise SystemExit(f"CrabNet compatibility patch: unexpected layout in {table_source}")
    text = text.replace(marker, '#include <stdlib.h>\n' + marker, 1)
    table_source.write_text(text, encoding="utf-8")
    print(f"Patched {table_source}")
else:
    print(f"Already compatible: {table_source}")

text = rand_source.read_text(encoding="utf-8")
# Fix the old typo flagged by modern AppleClang. The intended operation is to
# decrement the remaining random-number state, not assign negative one.
patched = text.replace("left =- 1;", "left -= 1;")
if patched != text:
    rand_source.write_text(patched, encoding="utf-8")
    print(f"Patched {rand_source}")
else:
    print(f"Already compatible: {rand_source}")
PY
