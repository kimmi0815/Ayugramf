# Bundled Japanese interface strings

The language selector exposes 日本語 even when Telegram's language list does
not include it. Its cloud language code is `ja-beta`, and its plural code is
`ja`. The official [Japanese translation page](https://translations.telegram.org/ja)
links to [the Japanese beta language pack](https://t.me/setlanguage/ja-beta).
The [Telegram API documentation](https://core.telegram.org/method/langpack.getLangPack)
accepts the pack name from that sharing link as `lang_code`.
The public translation website's `/ja/` path is not the cloud pack code.

During fresh-profile Debug app validation on 2026-10-06, a request for
`ja-beta` returned a difference whose language code was `ja-raw`. These are
accepted as aliases of the same Japanese pack. Selection, requests and the
serialized ID retain the official sharing-link name `ja-beta`. A shared ID
comparison is used both when routing current/base updates and when checking the
instance receiving the difference. A `ja-raw` response is applied only while
the current or base pack is Japanese; it cannot switch another language back to
Japanese. Applied cloud values and their server version are saved through the
normal language-pack cache path.

Two resources provide local defaults before any network response:

- `ja_core.strings`: 832 Telegram Desktop and fork keys. They cover the QR and
  phone login screens, main settings categories and common settings, chat-list
  text, language selection, common dialogs, and the macOS application menus.
- `ja_ayu.strings`: all 411 AyuGram keys declared in `lang.strings`, plus the four
  automatic message translation setting labels. This includes Ghost Mode and
  the other AyuGram menu/settings text.

This is a partial Telegram core fallback, not a full 8,324-key translation pack.
Other screens and newer core text use the normal `ja-beta` cloud pack and its
cache. An uncached key absent from these resources keeps the original English
value while offline. These resources translate interface text; incoming message
translation is controlled by the separate automatic translation settings.

The core snapshot uses approved `tr-value` entries from Telegram's public
Japanese TDesktop translation pages: Settings (first 200 rows), Log In,
Chat List, and General (first 400 rows). It excludes unapproved suggestions,
unknown keys, and one entry whose placeholders no longer match the checkout
(`lng_local_storage_cleared`). This yielded 769 known keys before supplements.
96 local supplements complete important menus and settings and replace some
snapshot wording; the final resource contains 736 snapshot values and 96 local
values. Ten local keys cover the parallel-export list, job count, waiting,
settings, failure and completion controls. Japanese `#other` values are used for both existing `#one` and `#other`
keys. Every declared placeholder is preserved.

`ja_core.sources.json` records retrieval date, page URLs, offsets, SHA-256 hashes
of each UTF-8 HTML page (the `more_html` payload for offset 200), adopted counts,
and the exact local supplement keys. Raw pages are not bundled. The offset-200
page was read using the public site's own Load More request: POST to the same
page URL with `offset=200&more=1` and `X-Requested-With: XMLHttpRequest`.
The [public frontend](https://translations.telegram.org/js/translations.js?113)
implements this pagination in `LoadMore.load`.

The AyuGram translations are based on the official language repository:

- Source: <https://github.com/AyuGram/Languages/blob/d86aafb98f55f5cf7d266c1cebcc5cae4d14d305/values/langs/ja/Shared.json>
- Source commit date: 2026-10-05T16:21:42Z
- Retrieved: 2026-10-06
- Raw source SHA-256: `70fb9e53864f41aa300c4065c99c93c5fe320ceff2b11b67fd786c4c0a12a3ea`

The 488 source keys were mapped using the existing desktop loader's rules:
omit Android-only entries, remove `_PC`, convert plural suffixes, prefix keys
with `ayu_`, and convert positional parameters to the placeholders declared in
`lang.strings`. Only existing desktop keys are included. 34 desktop values that
remained in English were translated locally. Product names and identifiers such
as GIF and TTL retain their spelling. The four `lng_ayu_auto_translate*` labels
are local translations.

Selection applies the local values and emits `Lang::Updated()` after plural
rules are updated. Logged-out screens and macOS menus can therefore refresh
without waiting for Telegram. Authenticated sessions retain the normal restart
confirmation, using local confirmation text. Japanese and default English
selections are saved before cloud requests, and their restart does not wait for
those requests. Switching to a concrete language cancels prior language metadata
and confirmation-string requests; stale metadata callbacks are also guarded.
AyuGram's separate JSON requests are cancelled before entering Japanese.

Loading a previously serialized `ja` or `ja-raw` selection normalizes its ID
to `ja-beta`, applies the local defaults, and saves the migrated pack. Legacy
`ja` resets its cloud version to zero for a fresh full pack; successful
`ja-beta` / `ja-raw` caches retain their server version and core overrides.
Serialized Japanese AyuGram values are skipped so
an old English fallback snapshot cannot override the completed local labels.
The separate AyuGram JSON cache/download is also skipped for all three Japanese
IDs: `ja`, `ja-beta` and `ja-raw`. Official and recent selector rows normalize
these aliases to one 日本語 entry, and all use the Japanese `ja` plural rules.
Telegram core cloud values take precedence over local defaults; the bundled
values do not become serialized cloud overrides or change the serialization
format. Deleting a cloud override restores the Japanese local value when one
exists, otherwise the original English value.

If the initial language list request fails or has not answered after three
seconds, the selector opens with default English, 日本語, and any recent
languages. Reopening it after the list arrives shows the fetched language list.

Run `python3 tests/japanese_ui/verify.py` after modifying these resources. It
checks key/placeholder coverage and the real lexical parser, and runs the
actual switch/reset/default methods, alias matching, current/base/stale response
routing, and raw-difference application with small standalone shims. Migration,
request cancellation and persistence have source-contract checks; a real Qt
serialization round-trip and cloud retrieval still require app validation.
