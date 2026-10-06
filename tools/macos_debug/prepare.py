import os
import pathlib
import platform
import sys

if sys.platform != "darwin" or platform.machine() != "arm64":
    raise SystemExit("This build profile requires an Apple Silicon Mac.")

repository = pathlib.Path(__file__).resolve().parents[2]
preparer = repository / "Telegram/build/prepare/prepare.py"
source = preparer.read_text(encoding="utf-8")
ending = "else:\n    runStages()\n"
if not source.endswith(ending):
    raise SystemExit("The upstream preparation dispatch has changed.")
source = source[:-len(ending)] + '''else:
    stages[:] = [entry for entry in stages
                 if entry["name"] not in {"breakpad", "crashpad"}]
    for entry in stages:
        if entry["name"] == "libiconv":
            entry["commands"] = entry["commands"].replace(
                'wget --timeout=30 --tries=2 -O libiconv.tar.gz ftp://ftp.gnu.org/gnu/libiconv/libiconv-$VERSION.tar.gz || wget -O libiconv.tar.gz https://ftp.gnu.org/pub/gnu/libiconv/libiconv-$VERSION.tar.gz',
                'curl -fL --connect-timeout 15 --max-time 120 --retry 2 -o libiconv.tar.gz https://mirrors.kernel.org/gnu/libiconv/libiconv-$VERSION.tar.gz\\n'
                'echo "3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8  libiconv.tar.gz" | shasum -a 256 -c -')
        if entry["name"].startswith("qt_"):
            entry["commands"] = entry["commands"].replace(
                '-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"',
                '-DCMAKE_OSX_ARCHITECTURES="arm64"')
    runStages()
'''
os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", "8")
sys.argv = [str(preparer), "skip-release", "silent", *sys.argv[1:]]
exec(compile(source, str(preparer), "exec"), {
    "__file__": str(preparer),
    "__name__": "__main__",
})
