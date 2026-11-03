from pathlib import Path
import re

p = Path('apps/openmw/CMakeLists.txt')
text = p.read_text(encoding='utf-8')

required = [
    r'openmw_add_executable\s*\(\s*openmw\b',
    r'target_link_libraries\s*\(\s*openmw\b',
    r'INSTALL\s*\(\s*TARGETS\s+openmw\b',
]
for pattern in required:
    if not re.search(pattern, text, re.IGNORECASE):
        raise SystemExit(f'Missing expected OpenMW target expression: {pattern}')

for pattern in [
    r'openmw_add_executable\s*\(\s*tes3mp\b',
    r'add_library\s*\(\s*tes3mp\b',
    r'target_link_libraries\s*\(\s*tes3mp\b',
    r'add_custom_command\s*\(\s*TARGET\s+tes3mp\b',
    r'INSTALL\s*\(\s*TARGETS\s+tes3mp\b',
]:
    if re.search(pattern, text, re.IGNORECASE):
        raise SystemExit(f'Multiplayer target leaked into single-player build: {pattern}')

print('OK: apps/openmw/CMakeLists.txt creates and uses the openmw target')
