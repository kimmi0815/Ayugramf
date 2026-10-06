#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def extract_block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError(f"Unclosed source block: {marker}")


def prepare_fixture(root, output):
    settings = (root / "Telegram/SourceFiles/ayu/ayu_settings.cpp").read_text()
    header = (root / "Telegram/SourceFiles/ayu/ayu_settings.h").read_text()
    tracker = (root / "Telegram/SourceFiles/history/view/history_view_translate_tracker.cpp").read_text()
    item = (root / "Telegram/SourceFiles/history/history_item.cpp").read_text()
    load_start = settings.index(
        "\tauto autoTranslation = AutoTranslationSettings();",
        settings.index("void from_json(const nlohmann::json &j, AyuSettings &s)"),
    )
    load_end = settings.index(
        "\n", settings.index("\ts._autoTranslation = NormalizeAutoTranslation", load_start),
    )
    replacements = {
        "@AUTO_SETTINGS_STRUCT@": extract_block(header, "struct AutoTranslationSettings") + ";",
        "@NORMALIZE_CODE@": extract_block(settings, "QString NormalizeTranslationLanguageCode"),
        "@NORMALIZE_SETTINGS@": extract_block(settings, "AutoTranslationSettings NormalizeAutoTranslation"),
        "@AUTO_LOADER@": "void Load(const nlohmann::json &j, AyuSettings &s) {\n" + settings[load_start:load_end] + "\n}",
        "@AUTO_SAVER@": "nlohmann::json Save(const AyuSettings &s) {\nreturn {\n" + "\n".join(
            line for line in settings.splitlines() if line.lstrip().startswith('{"autoTranslate')
        ) + "\n};\n}",
        "@RENDER_MATCHER@": extract_block(item, "bool TranslationMatchesTarget"),
        "@ITEM_SHOW_REQUEST@": extract_block(item, "bool HistoryItem::translationShowRequiresRequest"),
        "@TEXT_ACCESSOR@": extract_block(item, "const TextWithEntities &HistoryItem::translatedText()"),
        "@RICH_ACCESSOR@": extract_block(item, "auto HistoryItem::translatedRichPage()"),
        "@TARGET_METHOD@": extract_block(tracker, "LanguageId TranslateTracker::translationTarget"),
        "@AUTOMATIC_SOURCE_METHOD@": extract_block(tracker, "LanguageId TranslateTracker::automaticSource"),
        "@CONTENT_GUARD_METHOD@": extract_block(tracker, "bool TranslateTracker::requestContentUnchanged"),
        "@DISPLAY_METHOD@": extract_block(tracker, "void TranslateTracker::showTranslationIfDesired"),
        "@BATCH_LOOP@": extract_block(tracker, "for (auto i = _itemsToRequest.end(); i != _itemsToRequest.begin();)"),
        "@REQUEST_CONSTANTS@": "\n".join(line for line in tracker.splitlines() if line.startswith("constexpr auto kRequest")),
    }
    fixture = (Path(__file__).parent / "fixtures.cpp.in").read_text()
    for marker, source in replacements.items():
        if fixture.count(marker) != 1:
            raise ValueError(f"Expected exactly one fixture marker: {marker}")
        fixture = fixture.replace(marker, source)
    path = output / "automatic_translation.cpp"
    path.write_text(fixture)
    return path


def run(root, output, native):
    source = prepare_fixture(root, output)
    binary = output / "automatic_translation"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + [
        "-std=c++20", "-I" + str(root / "Telegram/SourceFiles"),
        str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    if native:
        if sys.platform != "darwin":
            raise SystemExit("Native recognition fixtures require macOS.")
        binary = output / "native_recognition"
        subprocess.run([
            "xcrun", "clang++", "-std=c++20", "-framework", "Foundation",
            "-framework", "NaturalLanguage",
            str(Path(__file__).parent / "native_recognition.mm"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-recognition", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        run(root, args.output_dir.resolve(), args.native_recognition)
    else:
        with tempfile.TemporaryDirectory(prefix="ayugram-auto-translation-") as directory:
            run(root, Path(directory), args.native_recognition)


if __name__ == "__main__":
    main()
