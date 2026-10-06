# Automatic translation fixtures

Run from the repository root:

```sh
python3 tests/automatic_translation/run_tests.py
```

This compiles a small C++20 fixture in a temporary directory. It does not
configure or build Telegram, install dependencies, contact translation services,
or read account data. Set `CXX` to choose a C++20 compiler. Python and the compiler
are the only runtime requirements; the fixture uses the repository's JSON header.

The runner extracts the current production normalization, JSON field loading and
saving, per-item target selection, automatic source tagging, display switching,
request content comparison, text/rich-page accessors, and batch selection loop.
The function bodies are inserted into `fixtures.cpp.in` at runtime so a change to
production logic is exercised directly.

The fixture currently checks 56 assertions covering:

- Default disabled state with Russian to Japanese configured; mixed invalid JSON
  types; language-code normalization, validation, deduplication, fallback, and
  round-trip persistence.
- Incoming source selection, target-language exclusion, multiple source languages,
  unknown languages, outgoing messages, disabled peers, manual target precedence,
  and manual suppression.
- A previously painted message remaining eligible after another paint generation;
  actual viewport visibility still controlling automatic requests.
- Text and rich-page display of explicitly tagged automatic translations, changing
  settings without an active tracker, and preserving cached manual Show Original
  behavior when the history target is cleared.
- Rejection of edited text, entity-only edits with unchanged text, and rich-page
  replacement with an unchanged text summary.
- Target, peer, and rich-page batch separation, plus the count and text-length
  request limits.

Qt strings/locales, reactive settings, message storage, and visibility queries are
explicit lightweight shims. The locale shim recognizes English, Russian,
Japanese, Ukrainian, and French only. These fixtures do not establish whole-app
compilation, generated-language integration, live widget interaction, asynchronous
provider cancellation, network delivery, or translation quality. The existing
rich-page path may use Telegram's API even when another provider is selected.

On macOS, six separate native language-recognition samples can also be run:

```sh
python3 tests/automatic_translation/run_tests.py --native-recognition
```

Those samples exercise Apple's `NLLanguageRecognizer` with the same top-hypothesis
selection used by the platform detector, for Russian, Japanese, English,
Ukrainian, and short Russian/Japanese greetings. They do not compile Telegram's Qt
wrapper and do not prove recognition accuracy for arbitrary or mixed text.

Use `--output-dir /private/tmp/ayugram-auto-tests` to retain the generated C++ and
executables for inspection.
