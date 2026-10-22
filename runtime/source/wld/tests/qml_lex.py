"""Mask literals/comments while preserving diagnostic line and column offsets."""
import re


def mask(text: str) -> str:
    out = list(text)
    i = 0
    while i < len(text):
        start = i
        ch = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end < 0 else end + 2
        elif ch in "\"'`":
            quote = ch
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
        elif ch == "/" and (
            not text[:i].rstrip()
            or text[:i].rstrip()[-1] in "=(:,[!&|?{;"
            or re.search(r"\b(?:return|throw)\s*$", text[:i])
        ):
            i += 1
            in_class = False
            while i < len(text) and text[i] != "\n":
                c = text[i]
                i += 1
                if c == "\\":
                    i += 1
                elif c == "[":
                    in_class = True
                elif c == "]":
                    in_class = False
                elif c == "/" and not in_class:
                    break
        else:
            i += 1
            continue
        for at in range(start, min(i, len(text))):
            if text[at] != "\n":
                out[at] = " "
    return "".join(out)
