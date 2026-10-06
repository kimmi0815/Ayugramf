# Japanese UI validation

From the repository root, run with Python 3.9 or newer and a C++17 compiler:

```sh
python3 tests/japanese_ui/verify.py
```

The runner also works from another directory when invoked by its absolute path.
It resolves the repository relative to its own path, uses the Python standard
library and `clang++` or `c++`, and removes its temporary standalone executables.
It does not download language sources, require Qt, build Telegram, or read
account data.

The resource checks cover both `ja_core.strings` and `ja_ayu.strings`: syntax,
duplicate/unknown/overlapping keys, complete AyuGram key coverage, important
core menus and automatic translation labels, exact placeholder names, conversion
of positional parameters, consistent Japanese plural forms, the two QRC paths,
provenance counts, UTF-8 encoding, LF line endings, and absence of a BOM.

For lexical parsing, the runner extracts the unchanged
`Lang::FileParser::readKeyValue` implementation and the unchanged
`base::parse::skipWhitespaces` helper from the checkout. It runs them over both
resources, compares every decoded value, verifies escaped quotes/backslashes/
newlines, and checks rejection of a missing semicolon and an unclosed string.
The harness replaces `QByteArray`, `QString`, `QLatin1String`, and the result
container with minimal standard-library wrappers. It exercises the callback
path. Generated key lookup is a sentinel stub; Python checks known keys and
placeholder names against `lang.strings` separately. It does not execute Qt's
Unicode representation or the runtime placeholder encoder.

A second harness extracts the actual `JapaneseLanguage`, `IsJapaneseLanguage`,
`AreLanguageIdsEquivalent`, `Instance::switchToId`, `reset`, `getDefaultValue`,
`applyDifferenceToMe`, and `CloudManager::packTypeFromId` methods without
modifying their bodies. It supplies the checked resource values as a fixture and
verifies that `ja`, `ja-beta` and `ja-raw` select `ja-beta`, all 1,237 local values
are present
before exactly one update notification, stale override state is cleared, plural
rules are updated first, English restores its defaults, and a derived instance
does not emit a root update. It also tests the full Japanese alias relation
matrix and rejection against English, French, empty and unknown IDs. The real
pack router accepts `ja-raw` for current Japanese and a Japanese base of French,
and rejects it after switching to English. The real difference handler applies
a raw-code cloud value, retains the selected beta ID, advances the server
version, retains the override for cache serialization, keeps untouched local
fallbacks, and emits an update.

Its explicit shims use UTF-8 `std::string` values,
a BMP-only `_q` literal converter, a map with fixture key indices, synchronous
event counters, small base/plural/sticker helpers, and a plain cloud-string /
difference fixture with number fields. The string handler and override/reset
helpers map that fixture to values without implementing MTProto or the production
placeholder encoder. It does not implement Qt, generated key indices, or the
production plural rule engine.

Source-contract checks additionally assert serialized Japanese ID normalization,
legacy `ja` version reset and raw-ID migration persistence,
skipping old Japanese AyuGram overrides, applying restore defaults, saving the
migration and local Japanese/English selections before the cloud request,
cancelling pending language metadata before a local selection can return, and
stale metadata guards on success, failure and confirmation. These are source
checks; they do not execute `QDataStream`, `Local::writeLangPack`, MTProto,
network callbacks, or rendering. A real Qt build/startup and fresh-profile UI
check are still needed to validate those boundaries.

Source provenance and fallback scope are recorded in
`Telegram/Resources/langs/ja_ayu.md` and `ja_core.sources.json`. Raw upstream
pages are not required by this runner and their hashes are not re-fetched here.
