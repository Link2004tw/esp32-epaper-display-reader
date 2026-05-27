from pdfminer.high_level import extract_text
book_name = "book_2_-_new_moon"
text = extract_text(f"{book_name}.pdf")

with open(f"{book_name}.txt", "w", encoding="utf-8") as f:
    f.write(text)

print("Done!")