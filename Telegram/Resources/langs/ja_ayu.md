# Bundled Japanese AyuGram strings

`ja_ayu.strings` supplies Japanese defaults for all 411 AyuGram keys in
`lang.strings`, plus the four automatic message translation setting labels.
Telegram's standard interface continues to use the `ja` cloud language pack.
The resource is a fallback and does not add fields to language serialization.
Explicit values from a Telegram cloud or custom language pack take precedence.

The AyuGram translations are based on the official language repository:

- Source: <https://github.com/AyuGram/Languages/blob/d86aafb98f55f5cf7d266c1cebcc5cae4d14d305/values/langs/ja/Shared.json>
- Source commit date: 2026-10-05T16:21:42Z
- Retrieved: 2026-10-06
- Raw source SHA-256: `70fb9e53864f41aa300c4065c99c93c5fe320ceff2b11b67fd786c4c0a12a3ea`

The 488 source keys were mapped using the existing desktop language loader's
rules: omit Android-only entries, remove `_PC`, convert plural suffixes to
`#one` / `#other`, prefix keys with `ayu_`, and convert positional parameters to
the placeholders declared in `lang.strings`. Only existing desktop keys are
included. Every declared placeholder is preserved.

34 desktop values that remained in English were translated locally. Product
names and common identifiers such as GIF and TTL retain their original spelling.
The four `lng_ayu_auto_translate*` setting labels are also local translations.
Japanese AyuGram values use this bundled snapshot rather than the separate
`Shared.json` cache/download, so upstream English fallback values cannot replace
the completed Japanese labels. Restoring a serialized Japanese language pack
also skips AyuGram values from an older downloaded snapshot. Standard Telegram
Japanese cloud updates and language selection persistence continue through the
existing code paths.

When updating the source snapshot, repeat the key and placeholder checks against
the current `lang.strings` and preserve the local supplements.
