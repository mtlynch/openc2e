import re
import os

def extract_raw_strings(content):
    # First pattern for normal R"(...)" strings in TEST blocks
    test_pattern = r'TEST\(caos,\s*(\w+)\)\s*{[^}]*?R"\((.*?)\)"'
    matches = re.finditer(test_pattern, content, re.DOTALL)

    os.makedirs('dict', exist_ok=True)

    for match in matches:
        test_name = match.group(1)
        raw_string = match.group(2)

        if not raw_string.strip():
            continue

        # Handle special_lexing case which is assigned to a variable first
        if test_name == 'special_lexing':
            raw_string = re.search(r'special_lexing_script = R"\((.*?)\)"', content, re.DOTALL).group(1)

        cleaned = '\n'.join(line.lstrip() for line in raw_string.splitlines())
        cleaned = cleaned.lstrip('\n')

        with open(f'dict/{test_name}.cos', 'w') as f:
            f.write(cleaned)

with open('src/openc2e/tests/CaosTest.cpp', 'r') as f:
    content = f.read()

extract_raw_strings(content)
