import arabic_reshaper
from bidi.algorithm import get_display

with open("output.txt", "r", encoding="utf-8") as f:
    text = f.read()

reshaped = arabic_reshaper.reshape(text)
bidi_text = get_display(reshaped)

with open("book_processed.txt", "w", encoding="utf-8") as f:
    f.write(bidi_text)