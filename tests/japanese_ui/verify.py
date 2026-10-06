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
OVERLAY = ROOT / "Telegram/Resources/langs/ja_ayu.strings"


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
    rows = entries(OVERLAY)
    values = dict(rows)
    require(len(rows) == len(values), "Duplicate Japanese resource key")
    require(len(rows) == len(OVERLAY.read_text(encoding="utf-8").splitlines()),
            "Invalid Japanese resource syntax")
    require({key for key in values if key.startswith("ayu_")}
            == {key for key in base if key.startswith("ayu_")},
            "Japanese resource does not cover all AyuGram keys")
    for key, value in values.items():
        require(key in base, f"Unknown key: {key}")
        require(set(PLACEHOLDER.findall(value))
                == set(PLACEHOLDER.findall(base[key])),
                f"Placeholder mismatch: {key}")
        require(not re.search(r"%[0-9]+\$[ds]", value),
                f"Unconverted positional parameter: {key}")
        if key.endswith("#one"):
            require(value == values[key.removesuffix("#one") + "#other"],
                    f"Japanese plural forms differ: {key}")
    require({"lng_ayu_auto_translate", "lng_ayu_auto_translate_about",
             "lng_ayu_auto_translate_from", "lng_ayu_auto_translate_to"}
            <= values.keys(), "Automatic translation labels missing")
    qrc = ROOT / "Telegram/Resources/qrc/ayu/ayu.qrc"
    matches = [
        entry for resource in ET.parse(qrc).getroot().findall("qresource")
        if resource.attrib["prefix"] == "/langs"
        for entry in resource.findall("file")
        if entry.attrib.get("alias") == "ja_ayu.strings"
    ]
    require(len(matches) == 1, "Japanese QRC entry missing or duplicated")
    require((qrc.parent / matches[0].text).resolve() == OVERLAY.resolve(),
            "Japanese QRC path does not resolve to the resource")
    for relative in (
        "Telegram/Resources/langs/ja_ayu.strings",
        "Telegram/Resources/langs/ja_ayu.md",
        "Telegram/Resources/qrc/ayu/ayu.qrc",
        "Telegram/SourceFiles/ayu/ayu_lang.cpp",
        "Telegram/SourceFiles/boxes/language_box.cpp",
        "Telegram/SourceFiles/lang/lang_instance.cpp",
        "Telegram/SourceFiles/lang/lang_instance.h",
        "tests/japanese_ui/verify.py",
        "tests/japanese_ui/README.md",
    ):
        data = (ROOT / relative).read_bytes()
        data.decode("utf-8")
        require(not data.startswith(b"\xef\xbb\xbf"), f"UTF-8 BOM: {relative}")
        require(b"\r" not in data, f"Non-LF line endings: {relative}")
    print(f"Resources: {len(values)} unique keys, all AyuGram keys covered, "
          "placeholders matched, QRC valid, UTF-8 LF without BOM")
    return rows


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


def check_actual_parser(expected):
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
        result = subprocess.run([str(executable), str(OVERLAY)], check=True,
                                capture_output=True, text=True)
    actual = [
        (key, bytes.fromhex(value).decode("utf-8"))
        for key, value in (line.split("\t", 1) for line in result.stdout.splitlines())
    ]
    require(actual == expected, "Actual parser output differs from resource values")
    print(f"Actual readKeyValue: {len(actual)} decoded values matched; "
          "escaped quotes, backslashes and newlines passed; malformed input rejected")


if __name__ == "__main__":
    check_actual_parser(check_resources())
