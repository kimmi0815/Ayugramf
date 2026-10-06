# Japanese UI validation

From the repository root, run with Python 3.9 or newer and a C++17 compiler:

```sh
python3 tests/japanese_ui/verify.py
```

The runner also works from another directory when invoked by its absolute path.
It resolves the repository relative to its own path. It uses only the
Python standard library and `clang++` or `c++`, writes its standalone executable
to a temporary directory, and removes that directory after execution. It does
not download language sources, require Qt, build Telegram, or read account data.

The resource checks cover syntax, duplicate and unknown keys, complete AyuGram
key coverage, automatic translation labels, placeholder names, conversion of
positional parameters, consistent Japanese plural forms, the QRC resource path,
UTF-8 encoding, LF line endings, and absence of a BOM.

For the parser check, the runner extracts the unchanged
`Lang::FileParser::readKeyValue` implementation from the current checkout and
the unchanged `base::parse::skipWhitespaces` helper. The standalone harness runs
them over `ja_ayu.strings`, compares all decoded values with the resource
checks, verifies escaped quotes/backslashes/newlines, and checks rejection of
a missing semicolon and an unclosed string.

The harness explicitly replaces `QByteArray`, `QString`, `QLatin1String`, and
the result container with minimal standard-library wrappers. The callback path
is exercised; generated key lookup is a sentinel stub, so known-key and
placeholder validation are performed separately against `lang.strings` in
Python. This verifies the real lexical parser body, not Qt behavior, generated
translation helpers, serialization, cloud retrieval, or the rendered app.
