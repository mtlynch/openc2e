import re
import os

def extract_raw_strings(content):
    test_pattern = r'TEST\(caos,\s*(\w+)\)\s*{[^}]*?R"\((.*?)\)"\s*\)'
    matches = re.finditer(test_pattern, content, re.DOTALL)

    os.makedirs('dict', exist_ok=True)

    for match in matches:
        test_name = match.group(1)
        raw_string = match.group(2)

        if not raw_string.strip():
            continue

        cleaned = '\n'.join(line.lstrip() for line in raw_string.splitlines())
        # Strip leading newlines while preserving other whitespace
        cleaned = cleaned.lstrip('\n')

        with open(f'dict/{test_name}.cos', 'w') as f:
            f.write(cleaned)

with open('src/openc2e/tests/CaosTest.cpp', 'r') as f:
    content = f.read()

extract_raw_strings(content)
