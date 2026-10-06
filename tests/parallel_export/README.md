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

The manager test also compiles the production `TakeoutSession::ForSession()`
factory. Its `TakeoutSession` stub only retains an account identity and a weak
lifetime token, and the controller checks that it receives the same account.
The stub has no acquisition, sharing, release, or transport implementation.

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

Run the takeout transport and lifecycle regression suites:

```sh
bash tests/parallel_export/run_takeout.sh
bash tests/parallel_export/run_takeout_session.sh
```

`run_takeout.sh` extracts the current production `ApiWrap::startMainSession()`,
`finishExport()`, `cancelExportFast()`, destructor, MTP error handler,
`mainRequest()`, and `RequestBuilder` definitions unchanged. It compiles those
definitions with the actual `Settings` header and actual
`export_takeout_session.cpp` and header. A reduced test declaration supplies
the ApiWrap members and injects settings and the shared session; the full
ApiWrap constructor, remaining methods and background actor are not compiled.
Its callback runner models the native actor's weak lifetime guard. The nine
tests check start/finish/cancel, settings permissions and media limits, starting
a second chat while the first still requests data, cancellation before self-id,
main acquisition and ready delivery, independent in-flight request cancellation,
and finish failure after the worker actor is destroyed.

The fake server maintains one active takeout ID per account. Creating another
takeout invalidates the previous ID; requests using it fail with
`TAKEOUT_INVALID`. Before the production shared-session fix, the first regression
was run and failed deterministically in approximately two seconds:

```text
FAIL starting_second_chat_keeps_first_chat_alive: first chat stopped after second chat started: TAKEOUT_INVALID; init calls=2
```

`run_takeout_session.sh` compiles the actual complete TakeoutSession source and
header, and extracts the unchanged account factory from `export_manager.cpp`.
Twenty tests exercise compatible leases, per-job completion and cancellation,
last-job cleanup, permission and file-limit incompatibility queues, joined
pending initialization, init failure fanout and queued retry, pending/queued
cancellation, late ready delivery, account factory reuse and replacement,
owner destruction before init response or callback delivery, MTP instance
destruction and closed-instance errors, reentrant ready callbacks, acquisitions
during finish, and finish failures. Init flood waits, server errors and negative
transport errors must be delivered as terminal errors to prevent retained
initialization from silently retrying after its account closes.

The transport, generated MTP types, QObject destruction/weak pointers,
`crl::on_main` dispatch and Main::Session lifetime are shims. Transport dispatch
and job callback queues can be advanced independently and deterministically.
The default MTP error-policy shim withholds terminal callbacks for errors that
native ConcurrentSender handles by retrying; it does not implement timed retry
scheduling. All suites use ASan and UBSan. They verify production source logic
against the observed one-active-takeout behavior, and do not perform Telegram
network requests, serialize MTP bytes, exercise real Qt or run UI/background
queues. Live export progress and actual service behavior still require the app.

Both takeout runners accept `--case <test_name>` for a single case. The optional
`TAKEOUT_API_SOURCE`, `TAKEOUT_SESSION_SOURCE`, and `TAKEOUT_MANAGER_SOURCE`
environment variables select temporary source copies for mutation checks;
normal invocations compile the current repository sources.
