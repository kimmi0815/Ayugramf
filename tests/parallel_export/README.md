# Parallel export manager regression tests

Run from the repository root:

```sh
bash tests/parallel_export/run.sh
```

The runner compiles the repository's actual `export_manager.cpp` and actual
`export_manager.h`, with the checked-out RPL, GSL and range-v3 headers. It runs
under AddressSanitizer and UndefinedBehaviorSanitizer. The temporary executable
is removed after the run; this does not build or launch Telegram and needs no
Qt SDK.

The tests cover chat/account/topic identity and reuse, cancelling one job,
late state delivery after removal, processing panel selection, a newly opened
settings panel while another export runs, completion/error/cancellation
transitions, account session replacement, scoped logout, quit confirmation,
pending callback ownership, and repeated observer subscription destruction.

`fake_domain.h` replaces Qt panels, sessions, peers and export controllers.
Their event streams and lifetimes use real RPL. State delivery is synchronous,
so these tests exercise reentrant callback destruction directly. They omit the
controller's background queue, UI painting, output writers and all network
requests. They do not establish Telegram server support for simultaneous
takeout sessions or test filesystem reservation.

Run the independent filesystem reservation tests from the repository root:

```sh
bash tests/parallel_export/run_output.sh
```

This runner extracts the current `NormalizePath()` function body unchanged
from `export_output_abstract.cpp`. It compiles that body under the same
sanitizers with small `QString`, `QDir`, `QFileInfo` and `QDate` shims. Directory
creation, existence checks and symlinks use real `std::filesystem` operations
inside the runner's temporary folder. The fixtures are removed after the run.

The output tests check 32 simultaneous reservations in the same parent,
existing file/directory/dangling symlink collisions, preservation of sibling
payloads, an uncreatable parent, failed candidate creation, and missing-parent
creation for an account export. Candidate failure uses an oversized date
returned by the date shim to trigger a real filesystem name-length failure.
This checks the production function's reservation protocol and failure
handling; the Qt implementations themselves are untested.
