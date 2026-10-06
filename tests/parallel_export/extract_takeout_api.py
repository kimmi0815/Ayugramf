from pathlib import Path
import re
import sys


def definition(source, signature):
    begin = source.index(signature)
    position = source.index("{", begin)
    depth = 0
    for index in range(position, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[begin:index + 1]
    raise RuntimeError(f"Incomplete definition: {signature}")


if sys.argv[1] == "--factory":
    source = Path(sys.argv[2]).read_text()
    signature = re.search(
        r"(?:auto|base::weak_qptr<TakeoutSession>) TakeoutSession::ForSession\(",
        source,
    )
    if not signature:
        raise RuntimeError("Missing production TakeoutSession::ForSession")
    function = definition(source, signature.group())
    Path(sys.argv[3]).write_text("namespace Export {\n\n" + function + "\n\n}\n")
    sys.exit(0)

if sys.argv[1] == "--controller-state":
    source = Path(sys.argv[2]).read_text()
    header = Path(sys.argv[3]).read_text()
    parts = [definition(header, "struct " + name) + ";" for name in [
        "PasswordCheckState",
        "ProcessingState",
        "ApiErrorState",
        "OutputErrorState",
        "CancelledState",
        "FinishedState",
    ]]
    parts.append(re.search(r"using State = std::variant<.*?;", header, re.S).group())
    Path(sys.argv[4]).write_text(
        "namespace Export {\n\n" + "\n\n".join(parts) + "\n\n}\n"
    )
    parts = [definition(source, signature) for signature in [
        "bool ControllerObject::stopped() const",
        "void ControllerObject::setState(",
        "void ControllerObject::waitingForTakeoutChanged(",
    ]]
    Path(sys.argv[5]).write_text(
        "namespace Export {\n\n" + "\n\n".join(parts) + "\n\n}\n"
    )
    sys.exit(0)

source = Path(sys.argv[1]).read_text()
builder_begin = source.index("template <typename Request>\nclass ApiWrap::RequestBuilder")
builder_end = source.index("ApiWrap::LoadedFileCache::LoadedFileCache", builder_begin)
parts = [source[builder_begin:builder_end].rstrip()]
parts.append(definition(source, "template <typename Request>\nauto ApiWrap::mainRequest"))
parts.extend(definition(source, signature) for signature in [
    "rpl::producer<bool> ApiWrap::waitingForTakeout() const",
    "void ApiWrap::startMainSession(",
    "void ApiWrap::finishExport(",
    "void ApiWrap::cancelExportFast(",
])
if "ApiWrap::~ApiWrap() {" in source:
    parts.append(definition(source, "ApiWrap::~ApiWrap() {"))
    parts.append(definition(source, "void ApiWrap::error(const MTP::Error &error) {"))
constant = re.search(r"constexpr auto kFileMaxSize = [^;]+;", source)
if not constant:
    raise RuntimeError("Missing production kFileMaxSize")
Path(sys.argv[2]).write_text(
    "namespace Export {\nnamespace {\n"
    + constant.group()
    + "\n}\n\n"
    + "\n\n".join(parts)
    + "\n\n}\n"
)
