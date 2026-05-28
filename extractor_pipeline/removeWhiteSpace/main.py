import re


def clean_html_tags(text):
    return re.sub(r'<[^>]+>', '', text)


def is_all_ascii(text):
    return all(c.isascii() for c in text)


def clean_file(input_path, output_path, remove_inner_spaces=False, remove_english=False):
    with open(input_path, 'r', encoding='utf-8') as infile:
        text = infile.read()

    text = clean_html_tags(text)
    lines = text.split('\n')
    cleaned_lines = []
    last_blank = False

    for line in lines:
        stripped = line.strip()

        if not stripped:
            if not last_blank:
                cleaned_lines.append('')
                last_blank = True
            continue

        last_blank = False

        if remove_inner_spaces:
            stripped = " ".join(stripped.split())

        if remove_english and is_all_ascii(stripped):
            continue

        cleaned_lines.append(stripped)

    while cleaned_lines and not cleaned_lines[0]:
        cleaned_lines.pop(0)
    while cleaned_lines and not cleaned_lines[-1]:
        cleaned_lines.pop()

    with open(output_path, 'w', encoding='utf-8') as outfile:
        outfile.write("\n".join(cleaned_lines))


clean_file("book_2_-_new_moon.txt", "output.txt", remove_inner_spaces=True, remove_english=True)