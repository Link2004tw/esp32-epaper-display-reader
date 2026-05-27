import re


def clean_html_tags(text):
    return re.sub(r'<[^>]+>', '', text)


def clean_file(input_path, output_path, remove_inner_spaces=False, remove_english=False):
    with open(input_path, 'r', encoding='utf-8') as infile:
        lines = infile.readlines()

    cleaned_lines = []

    for line in lines:
        stripped = line.strip()

        if not stripped:
            continue

        stripped = clean_html_tags(stripped)

        if remove_inner_spaces:
            stripped = " ".join(stripped.split())

        if remove_english:
            tokens = stripped.split()
            filtered = [t for t in tokens if not t.isascii() or len(t) < 2]
            stripped = " ".join(filtered)

        if stripped:
            cleaned_lines.append(stripped)

    with open(output_path, 'w', encoding='utf-8') as outfile:
        outfile.write("\n".join(cleaned_lines))


clean_file("input.txt", "output.txt", remove_inner_spaces=True, remove_english=True)