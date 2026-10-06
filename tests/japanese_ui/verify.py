import json
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = re.compile(r'^"([A-Za-z0-9_#]+)"\s*=\s*"((?:[^"\\]|\\.)*)";$', re.M)
PLACEHOLDER = re.compile(r"\{([A-Za-z0-9_]+)\}")
RESOURCES = tuple(
    ROOT / f"Telegram/Resources/langs/{name}.strings"
    for name in ("ja_core", "ja_ayu")
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def entries(path):
    return [
        (match[1], json.loads('"' + match[2] + '"'))
        for match in ENTRY.finditer(path.read_text(encoding="utf-8"))
    ]


def check_resources():
    base = dict(entries(ROOT / "Telegram/Resources/langs/lang.strings"))
    combined = {}
    resources = {}
    qrc = ROOT / "Telegram/Resources/qrc/ayu/ayu.qrc"
    for path in RESOURCES:
        rows = entries(path)
        values = dict(rows)
        require(len(rows) == len(values), f"Duplicate key: {path.name}")
        require(len(rows) == len(path.read_text(encoding="utf-8").splitlines()),
                f"Invalid resource syntax: {path.name}")
        require(not combined.keys() & values.keys(), "Overlapping resource keys")
        combined.update(values)
        resources[path] = rows
        matches = [
            entry for resource in ET.parse(qrc).getroot().findall("qresource")
            if resource.attrib["prefix"] == "/langs"
            for entry in resource.findall("file")
            if entry.attrib.get("alias") == path.name
        ]
        require(len(matches) == 1, f"Missing or duplicate QRC entry: {path.name}")
        require((qrc.parent / matches[0].text).resolve() == path.resolve(),
                f"QRC path does not resolve: {path.name}")
    require({key for key in combined if key.startswith("ayu_")}
            == {key for key in base if key.startswith("ayu_")},
            "Japanese resource does not cover all AyuGram keys")
    for key, value in combined.items():
        require(key in base, f"Unknown key: {key}")
        require(set(PLACEHOLDER.findall(value))
                == set(PLACEHOLDER.findall(base[key])),
                f"Placeholder mismatch: {key}")
        require(not re.search(r"%[0-9]+\$[ds]", value),
                f"Unconverted positional parameter: {key}")
        if key.endswith("#one"):
            require(value == combined[key.removesuffix("#one") + "#other"],
                    f"Japanese plural forms differ: {key}")
    require({"lng_ayu_auto_translate", "lng_ayu_auto_translate_about",
             "lng_ayu_auto_translate_from", "lng_ayu_auto_translate_to",
             "lng_menu_settings", "lng_settings_language", "lng_languages",
             "lng_intro_qr_title", "lng_mac_menu_file", "lng_mac_menu_edit",
             "lng_mac_menu_window", "lng_sure_save_language"}
            <= combined.keys(), "Required interface labels missing")
    provenance = json.loads((ROOT / "Telegram/Resources/langs/ja_core.sources.json")
                            .read_text(encoding="utf-8"))
    require(provenance["bundled_core_keys"] == len(resources[RESOURCES[0]]),
            "Provenance core key count differs")
    require(set(provenance["local_supplement_keys"]) <= combined.keys(),
            "Provenance refers to missing local supplements")
    for relative in (
        "Telegram/Resources/langs/ja_core.strings",
        "Telegram/Resources/langs/ja_core.sources.json",
        "Telegram/Resources/langs/ja_ayu.strings",
        "Telegram/Resources/langs/ja_ayu.md",
        "Telegram/Resources/qrc/ayu/ayu.qrc",
        "Telegram/SourceFiles/ayu/ayu_lang.cpp",
        "Telegram/SourceFiles/boxes/language_box.cpp",
        "Telegram/SourceFiles/lang/lang_cloud_manager.cpp",
        "Telegram/SourceFiles/lang/lang_instance.cpp",
        "Telegram/SourceFiles/lang/lang_instance.h",
        "tests/japanese_ui/verify.py",
        "tests/japanese_ui/README.md",
    ):
        data = (ROOT / relative).read_bytes()
        data.decode("utf-8")
        require(not data.startswith(b"\xef\xbb\xbf"), f"UTF-8 BOM: {relative}")
        require(b"\r" not in data, f"Non-LF line endings: {relative}")
    print(f"Resources: {len(combined)} unique keys "
          f"({len(resources[RESOURCES[0]])} core, {len(resources[RESOURCES[1]])} Ayu), "
          "all AyuGram keys covered, placeholders matched, QRC valid, UTF-8 LF")
    return resources


SHIM_PREFIX = r'''
#include <cassert>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using ushort = unsigned short;
constexpr auto kKeysCount = ushort(20000);
#define Assert(value) assert(value)
struct QLatin1String {
    const char *data = nullptr;
    size_t size = 0;
    QLatin1String(const char *value, size_t count) : data(value), size(count) {}
    std::string string() const { return { data, size }; }
};
struct QByteArray : std::string {
    using std::string::string;
    void append(char value) { push_back(value); }
    void append(const char *value, size_t count) { std::string::append(value, count); }
};
struct QString : std::string {
    using std::string::string;
    QString(const std::string &value) : std::string(value) {}
    QString arg(QLatin1String value) const {
        auto result = std::string(*this);
        const auto index = result.find("%1");
        if (index != std::string::npos) result.replace(index, 2, value.string());
        return result;
    }
    static QString fromUtf8(const QByteArray &value) { return std::string(value); }
};
QString operator""_q(const char16_t *value, size_t count) {
    auto result = std::string();
    for (auto i = size_t(0); i != count; ++i) result.push_back(char(value[i]));
    return result;
}
ushort GetKeyIndex(QLatin1String) { return kKeysCount; }
struct Result : std::map<ushort, QString> {
    void insert(ushort key, QString value) { (*this)[key] = std::move(value); }
};
namespace base::parse {
'''
SHIM_CLASS = r'''
}
namespace Lang {
class FileParser {
public:
    std::set<ushort> _request;
    std::function<void(QLatin1String, const QByteArray&)> _callback;
    Result _result;
    std::string failure;
    bool error(const QString &message) { failure = message; return false; }
    bool readKeyValue(const char *&from, const char *end);

};
'''
SHIM_MAIN = r'''
}
std::pair<std::vector<std::pair<std::string, std::string>>, std::string> Parse(std::string content) {
    auto parser = Lang::FileParser();
    auto output = std::vector<std::pair<std::string, std::string>>();
    parser._callback = [&](QLatin1String key, const QByteArray &value) {
        output.emplace_back(key.string(), value);
    };
    auto cursor = content.data();
    const auto end = cursor + content.size();
    const char *from = cursor;
    while (from != end && parser.readKeyValue(from, end)) {}
    return { output, parser.failure };
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    auto input = std::ifstream(argv[1]);
    if (!input) return 3;
    auto text = std::ostringstream();
    text << input.rdbuf();
    const auto [values, error] = Parse(text.str());
    if (!error.empty()) { std::cerr << error << '\n'; return 4; }
    const auto [valid, validError] = Parse("\"test\" = \"line1\\nline2\\\"quoted\\\"\\\\tail\";\n");
    if (!validError.empty() || valid.size() != 1
        || valid[0].second != "line1\nline2\"quoted\"\\tail") return 5;
    const auto [incomplete, incompleteError] = Parse("\"test\" = \"value\"\n");
    if (incompleteError.empty()) return 6;
    const auto [unclosed, unclosedError] = Parse("\"test\" = \"unclosed\n");
    if (unclosedError.empty()) return 7;
    auto unique = std::set<std::string>();
    for (const auto &[key, value] : values) {
        if (!unique.insert(key).second) return 8;
        std::cout << key << '\t';
        for (const auto byte : value) {
            std::cout << std::hex << std::setfill('0') << std::setw(2)
                << int(static_cast<unsigned char>(byte));
        }
        std::cout << '\n';
    }
}
'''


def check_actual_parser(resources):
    compiler = shutil.which("clang++") or shutil.which("c++")
    require(compiler is not None, "A C++17 compiler is required")
    parser = (ROOT / "Telegram/SourceFiles/lang/lang_file_parser.cpp").read_text(
        encoding="utf-8")
    method = parser[parser.index("bool FileParser::readKeyValue("):
                    parser.index("\nQByteArray FileParser::ReadFile(")]
    helper = (ROOT / "Telegram/lib_base/base/parse_helper.h").read_text(
        encoding="utf-8")
    whitespace = helper[helper.index("inline bool skipWhitespaces("):
                        helper.index("\ninline QLatin1String readName(")]
    source = SHIM_PREFIX + whitespace + SHIM_CLASS + method + SHIM_MAIN
    with tempfile.TemporaryDirectory(prefix="ayugram-japanese-parser-") as folder:
        folder = Path(folder)
        cpp, executable = folder / "verify.cpp", folder / "verify"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(executable)],
                       check=True)
        for path, expected in resources.items():
            result = subprocess.run([str(executable), str(path)], check=True,
                                    capture_output=True, text=True)
            actual = [
                (key, bytes.fromhex(value).decode("utf-8"))
                for key, value in (line.split("\t", 1)
                                   for line in result.stdout.splitlines())
            ]
            require(actual == expected, f"Actual parser output differs: {path.name}")
    count = sum(len(rows) for rows in resources.values())
    print(f"Actual readKeyValue: {count} decoded values matched; "
          "escaped quotes, backslashes and newlines passed; malformed input rejected")


SWITCH_SHIM = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
using ushort = unsigned short;
using QByteArray = std::string;
struct QString : std::string {
    using std::string::string;
    QString(const std::string &value) : std::string(value) {}
    bool isEmpty() const { return empty(); }
};
QString operator""_q(const char16_t *value, size_t count) {
    auto result = QString();
    for (auto i = size_t(0); i != count; ++i) {
        const auto code = value[i];
        assert(code < 0xd800 || code > 0xdfff);
        if (code < 0x80) {
            result.push_back(char(code));
        } else if (code < 0x800) {
            result.push_back(char(0xc0 | (code >> 6)));
            result.push_back(char(0x80 | (code & 0x3f)));
        } else {
            result.push_back(char(0xe0 | (code >> 12)));
            result.push_back(char(0x80 | ((code >> 6) & 0x3f)));
            result.push_back(char(0x80 | (code & 0x3f)));
        }
    }
    return result;
}
namespace ranges {
    template <typename T> void fill(T &target, int value) {
        std::fill(target.begin(), target.end(), value);
    }
}
#define Expects(value) assert(value)
namespace Lang {
struct Language { QString id, pluralId, baseId, name, nativeName; };
enum class Pack { None, Current, Base };
struct CloudString { QByteArray key; QString value; };
struct IntField { int v = 0; };
struct StringsField { std::vector<CloudString> v; };
struct MTPDlangPackDifference {
    QString code;
    IntField fromVersion, version;
    StringsField strings;
    const QString &vlang_code() const { return code; }
    const IntField &vfrom_version() const { return fromVersion; }
    const IntField &vversion() const { return version; }
    const StringsField &vstrings() const { return strings; }
};
QString qs(const QString &code) { return code; }
template <typename Set, typename Reset>
void HandleString(const CloudString &value, Set set, Reset) {
    set(value.key, value.value);
}
struct Event {
    int count = 0;
    std::function<void()> callback;
    void fire(int) { ++count; if (callback) callback(); }
    void fire_copy(const QString &) {}
};
std::map<ushort, QString> bundled;
const std::map<ushort, QString> &JapaneseBundledValues() { return bundled; }
QString GetOriginalValue(ushort) { return "English"; }
QString PrepareTestValue(QString, char) { return "Test"; }
QString LanguageIdOrDefault(const QString &id) { return id.isEmpty() ? "en" : id; }
class Instance {
public:
    QString _id, _pluralId, _name, _nativeName;
    QString _customFilePathAbsolute, _customFilePathRelative;
    QByteArray _customFileContent;
    int _version = 0;
    Instance *_derived = nullptr;
    Instance *_base = nullptr;
    std::vector<QString> _values;
    std::vector<int> _nonDefaultSet;
    std::map<QByteArray, QByteArray> _nonDefaultValues;
    Event _updated, _idChanges;
    bool pluralRulesUpdated = false;
    void setBaseId(const QString &baseId, const QString &) { assert(baseId.empty()); }
    QString id() const { return _id; }
    QString baseId() const { return _base ? _base->_id : QString(); }
    void updateChoosingStickerReplacement() {}
    void updatePluralRules() { pluralRulesUpdated = true; }
    void switchToId(const Language &data);
    void reset(const Language &data);
    QString getDefaultValue(ushort key) const;
    void applyDifferenceToMe(const MTPDlangPackDifference &difference);
    void applyValue(const QByteArray &key, const QString &value) {
        _nonDefaultValues[key] = value;
        _nonDefaultSet[std::stoi(key)] = 1;
        _values[std::stoi(key)] = value;
    }
    void resetValue(const QByteArray &key) {
        _nonDefaultValues.erase(key);
        _nonDefaultSet[std::stoi(key)] = 0;
        _values[std::stoi(key)] = getDefaultValue(ushort(std::stoi(key)));
    }

};
class CloudManager {
public:
    Instance &_langpack;
    explicit CloudManager(Instance &instance) : _langpack(instance) {}
    Pack packTypeFromId(const QString &id) const;

};
'''
SWITCH_MAIN = r'''
}
int main() {
    for (const auto &first : { "ja", "ja-beta", "ja-raw" }) {
        for (const auto &second : { "ja", "ja-beta", "ja-raw" }) {
            assert(Lang::AreLanguageIdsEquivalent(first, second));
        }
        for (const auto &other : { "en", "fr", "", "ja-other" }) {
            assert(!Lang::AreLanguageIdsEquivalent(first, other));
            assert(!Lang::AreLanguageIdsEquivalent(other, first));
        }
    }
    assert(Lang::AreLanguageIdsEquivalent("fr", "fr"));
    for (const auto &id : { "ja", "ja-beta", "ja-raw" }) {
        auto instance = Lang::Instance();
        instance._values.resize(Lang::bundled.size() + 1);
        instance._nonDefaultSet.resize(instance._values.size(), 1);
        instance._nonDefaultValues["stale"] = "English";
        instance._updated.callback = [&] {
            assert(instance._id == "ja-beta");
            assert(instance._pluralId == "ja");
            assert(instance.pluralRulesUpdated);
            for (const auto &[key, value] : Lang::bundled) {
                assert(instance._values[key] == value);
                assert(instance._nonDefaultSet[key] == 0);
            }
            assert(instance._values.back() == "English");
            assert(instance._nonDefaultValues.empty());
        };
        instance.switchToId({ id });
        assert(instance._updated.count == 1);
        instance._updated.callback = {};
        instance.switchToId({ "en" });
        assert(instance._updated.count == 2);
        for (const auto &value : instance._values) assert(value == "English");
        instance._derived = &instance;
        instance.switchToId({ "ja-beta" });
        assert(instance._updated.count == 2);
    }
    auto instance = Lang::Instance();
    instance._values.resize(Lang::bundled.size() + 1);
    instance._nonDefaultSet.resize(instance._values.size());
    instance.switchToId({ "ja-beta" });
    auto manager = Lang::CloudManager(instance);
    assert(manager.packTypeFromId("ja-raw") == Lang::Pack::Current);
    instance.applyDifferenceToMe({ "ja-raw", { 0 }, { 42 }, {{{ "0", "Cloud Japanese" }}} });
    assert(instance._id == "ja-beta" && instance._version == 42);
    assert(instance._values[0] == "Cloud Japanese");
    assert(instance._nonDefaultValues["0"] == "Cloud Japanese");
    assert(instance._values[1] == Lang::bundled[1]);
    assert(instance._updated.count == 2);
    instance.switchToId({ "en" });
    assert(manager.packTypeFromId("ja-raw") == Lang::Pack::None);
    assert(instance._values[0] == "English" && instance._version == 0);
    auto base = Lang::Instance();
    base._id = "ja-beta";
    instance._id = "fr";
    instance._base = &base;
    assert(manager.packTypeFromId("ja-raw") == Lang::Pack::Base);
    assert(manager.packTypeFromId("de") == Lang::Pack::None);
}
'''


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor] + "\n"


def check_actual_switch(resources):
    source = (ROOT / "Telegram/SourceFiles/lang/lang_instance.cpp").read_text(
        encoding="utf-8")
    signatures = (
        "Language JapaneseLanguage()",
        "bool IsJapaneseLanguage(const QString &id)",
        "bool AreLanguageIdsEquivalent(const QString &first, const QString &second)",
        "void Instance::switchToId(const Language &data)",
        "void Instance::reset(const Language &data)",
        "QString Instance::getDefaultValue(ushort key) const",
        "void Instance::applyDifferenceToMe(",
    )
    values = [value for rows in resources.values() for _, value in rows]
    assignments = "\n".join(
        f"Lang::bundled[{index}] = {json.dumps(value, ensure_ascii=False)};"
        for index, value in enumerate(values)
    )
    main = SWITCH_MAIN.replace("int main() {", "int main() {\n" + assignments)
    cloud = (ROOT / "Telegram/SourceFiles/lang/lang_cloud_manager.cpp").read_text(
        encoding="utf-8")
    pack = method(cloud, "Pack CloudManager::packTypeFromId(const QString &id) const")
    harness = (SWITCH_SHIM + "\n".join(method(source, item) for item in signatures)
               + pack + main)
    with tempfile.TemporaryDirectory(prefix="ayugram-japanese-switch-") as folder:
        folder = Path(folder)
        cpp, executable = folder / "verify.cpp", folder / "verify"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([shutil.which("clang++") or shutil.which("c++"),
                        "-std=c++17", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    restore = method(source, "void Instance::fillFromSerialized(")
    require('_id = IsJapaneseLanguage(id) ? JapaneseLanguage().id : id;' in restore,
            "Serialized legacy ja migration missing")
    require('_version = (id == u"ja"_q) ? 0 : version;' in restore,
            "Legacy ja version is not reset for a full beta download")
    require('IsJapaneseLanguage(_id)\n\t\t\t&& nonDefaultStrings[i].startsWith("ayu_")'
            in restore, "Stale Japanese Ayu values are not skipped")
    require("applyBundledValues();" in restore and "Local::writeLangPack();" in restore,
            "Restore fallback or migration persistence missing")
    cloud = (ROOT / "Telegram/SourceFiles/lang/lang_cloud_manager.cpp").read_text(
        encoding="utf-8")
    switch = method(cloud, "void CloudManager::performSwitch(const Language &data)")
    require(switch.index("Local::writeLangPack();")
            < switch.index("requestLangPackDifference(Pack::Current);"),
            "Japanese selection is not persisted before the network request")
    concrete = method(cloud, "void CloudManager::switchToLanguage(const Language &data)")
    require(concrete.index("base::take(_switchingToLanguageRequest)")
            < concrete.index("if (_langpack.id() == data.id"),
            "Pending language metadata request survives a local selection")
    require("_switchingToLanguageId = QString();" in concrete
            and "_switchingToLanguageWarning = false;" in concrete,
            "Pending metadata desired language or warning is not reset")
    metadata = method(cloud, "void CloudManager::sendSwitchingToLanguageRequest()")
    require(metadata.count("_switchingToLanguageId != requestedId") == 3,
            "Metadata success, failure or confirmation misses a stale reply guard")
    require("|| _langpack.id() == DefaultLanguageId()" in switch,
            "Local English selection is not persisted")
    require('if (!_derived && IsJapaneseLanguage(id) && id != _id)' in restore,
            "Serialized raw ID normalization is not persisted")
    print(f"Actual switch/reset/default methods: {len(values)} bundled values applied "
          "before one update; ja/raw -> ja-beta; English restored; derived silent. "
          "Actual alias matrix, current/base/stale response routing and raw difference "
          "application/version/overrides passed. Restore/persistence source contracts passed.")


if __name__ == "__main__":
    resources = check_resources()
    check_actual_parser(resources)
    check_actual_switch(resources)
