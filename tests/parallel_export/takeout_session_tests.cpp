#include "takeout_domain.h"

#include "export/export_takeout_session.h"
#include "main/main_session.h"
#include "takeout_factory.inc"

namespace {

using Session = Export::TakeoutSession;
using Flag = MTPaccount_InitTakeoutSession::Flag;

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Job {
	std::optional<uint64> id;
	std::vector<std::string> errors;
	std::vector<bool> waiting;
	std::shared_ptr<Session::Lease> lease = std::make_shared<Session::Lease>();
	std::deque<FnMut<void()>> callbacks;
	bool deferCallbacks = false;

	Job(MTPaccount_InitTakeoutSession::Flags flags, int64 sizeLimit) {
		lease->flags = flags;
		lease->sizeLimit = sizeLimit;
		lease->runner = [=](FnMut<void()> callback) {
			if (deferCallbacks) {
				callbacks.push_back(std::move(callback));
			} else {
				callback();
			}
		};
		lease->ready = [=](uint64 value) { id = value; };
		lease->waiting = [=](bool value) { waiting.push_back(value); };
		lease->failed = [=](const MTP::Error &error) { errors.push_back(error.name); };
	}

	void drain() {
		while (!callbacks.empty()) {
			auto next = std::move(callbacks.front());
			callbacks.pop_front();
			next();
		}
	}
};

struct Fixture {
	MTP::Server server;
	Main::Session owner = Main::Session(server);
	base::weak_qptr<Session> session = Session::ForSession(&owner);

	void requireValid(const Job &job) {
		Require(job.id.has_value(), "job has no takeout id");
		try {
			server.execute(MTPInvokeWithTakeout<MTPusers_GetUsers>(
				MTP_long(*job.id),
				MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf()))));
		} catch (const MTP::Error &error) {
			throw std::runtime_error("job stopped: " + error.name);
		}
	}
};

void ParallelCompatibleJobsUseOneTakeout() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels | Flag::f_files, 64);
	auto second = Job(Flag::f_message_channels | Flag::f_files, 32);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	fixture.requireValid(first);
	fixture.requireValid(second);
	Require(first.id == second.id, "compatible jobs got different takeout ids");
	Require(fixture.server.initializations.size() == 1, "multiple init calls");
	fixture.session->release(first.lease, false);
	Require(fixture.server.finishes.empty(), "one cancellation finished survivor");
	fixture.requireValid(second);
	fixture.session->release(second.lease, true);
	Require(fixture.server.finishes.size() == 1, "last job did not clean up once");
	Require(fixture.server.finishes.front().second == 0, "cancelled group marked success");
}

void IndividualCompletionKeepsSurvivorAndLastCompletes() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	auto firstDone = false;
	auto secondDone = false;
	fixture.session->release(first.lease, true, [&] { firstDone = true; });
	Require(firstDone, "individual completion callback did not run");
	Require(fixture.server.finishes.empty(), "individual completion finished survivor");
	fixture.requireValid(second);
	fixture.session->release(second.lease, true, [&] { secondDone = true; });
	Require(secondDone, "last completion callback did not run");
	Require(fixture.server.finishes.size() == 1, "last completion was not singular");
	Require(fixture.server.finishes.front().second == 1, "success flag was lost");
}

void IncompatiblePermissionWaitsForExistingGroup() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_contacts | Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	Require(!second.id, "permission widening started an incompatible takeout");
	Require(fixture.server.initializations.size() == 1, "queued job initialized early");
	fixture.requireValid(first);
	fixture.session->release(first.lease, true);
	fixture.requireValid(second);
	Require(fixture.server.initializations.size() == 2, "queued job never initialized");
	Require(fixture.server.initializations.back().flags == 17, "queued permission flags lost");
}

void LargerFileLimitWaitsAndSmallerLimitShares() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels | Flag::f_files, 32);
	auto smaller = Job(Flag::f_message_channels | Flag::f_files, 16);
	auto larger = Job(Flag::f_message_channels | Flag::f_files, 64);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(smaller.lease);
	fixture.session->acquire(larger.lease);
	Require(first.id == smaller.id, "smaller limit did not share the takeout");
	Require(!larger.id, "larger limit silently shared an insufficient takeout");
	fixture.session->release(first.lease, true);
	Require(!larger.id, "queued larger job invalidated remaining smaller job");
	fixture.requireValid(smaller);
	fixture.session->release(smaller.lease, true);
	fixture.requireValid(larger);
	Require(fixture.server.initializations.back().maxSize == 64, "larger limit was truncated");
}

void ConcurrentPendingInitializationsJoinOneRequest() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	Require(fixture.server.pending.size() == 1, "compatible pending init was duplicated");
	fixture.server.drain();
	Require(first.id == second.id, "pending init jobs got different ids");
	fixture.requireValid(first);
	fixture.requireValid(second);
}

void InitializationFailureFansOutAndQueuedJobRetries() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	fixture.server.failNextInit = MTP::Error{ "TAKEOUT_INIT_DELAY_30" };
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_message_channels, 0);
	auto queued = Job(Flag::f_contacts, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	fixture.session->acquire(queued.lease);
	fixture.server.drain();
	Require(first.errors == std::vector<std::string>{ "TAKEOUT_INIT_DELAY_30" },
		"first init error was not delivered exactly once");
	Require(second.errors == first.errors, "joined job did not receive init error");
	Require(!first.id && !second.id, "failed init produced an id");
	fixture.requireValid(queued);
	Require(fixture.server.initializations.size() == 2, "queued retry did not run");
}

void QueuedCancellationCannotStartOrAffectActiveJob() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto queued = Job(Flag::f_contacts, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(queued.lease);
	fixture.session->release(queued.lease, false);
	fixture.requireValid(first);
	fixture.session->release(first.lease, true);
	Require(!queued.id, "cancelled queued job started");
	Require(fixture.server.initializations.size() == 1, "cancelled queued job initialized");
	Require(fixture.server.finishes.front().second == 1, "queued cancellation changed active result");
}

void PendingCancellationCleansReturnedSession() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	auto first = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.session->release(first.lease, false);
	fixture.server.drain();
	Require(!first.id, "cancelled pending job was started");
	Require(fixture.server.finishes.size() == 1, "returned orphaned takeout was leaked");
	Require(fixture.server.finishes.front().second == 0, "orphaned takeout marked success");
	Require(!fixture.server.active, "orphaned takeout remained active");
}

void CancelledReadyCallbackCannotReviveJob() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	first.deferCallbacks = true;
	fixture.session->acquire(first.lease);
	Require(first.callbacks.size() == 2, "initializing and ready callbacks were not queued");
	fixture.session->release(first.lease, false);
	first.drain();
	Require(!first.id, "late ready callback revived a cancelled job");
	Require(first.waiting.empty(), "cancelled job received waiting callbacks");
}

void QueuedWaitingClearsBeforeInitializationCompletes() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto queued = Job(Flag::f_contacts, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(queued.lease);
	Require(queued.waiting == std::vector<bool>{ true }, "queued lease did not report waiting");
	fixture.server.defer = true;
	fixture.session->release(first.lease, true);
	Require(fixture.server.drainOne(), "prior finish response was not pending");
	Require(queued.waiting == std::vector<bool>{ true, false },
		"promoted lease did not clear waiting before initialization response");
	Require(!queued.id, "promoted lease completed initialization prematurely");
	fixture.server.drain();
	fixture.requireValid(queued);
	Require(!queued.waiting.back(), "ready lease retained waiting state");
}

void CompatiblePendingAndReadyLeasesNeverReportWaiting() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	auto first = Job(Flag::f_message_channels, 0);
	auto joined = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(joined.lease);
	Require(first.waiting == std::vector<bool>{ false }, "initializing lease reported waiting");
	Require(joined.waiting == first.waiting, "compatible initializing lease reported waiting");
	fixture.server.drain();
	fixture.server.defer = false;
	auto ready = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(ready.lease);
	Require(ready.waiting == std::vector<bool>{ false }, "compatible ready lease reported waiting");
	Require(std::ranges::none_of(first.waiting, [](bool value) { return value; }),
		"initial lease ever reported queue waiting");
	Require(std::ranges::none_of(joined.waiting, [](bool value) { return value; }),
		"joined lease ever reported queue waiting");
}

void CancelledQueuedWaitingNotificationIsSuppressed() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto queued = Job(Flag::f_contacts, 0);
	queued.deferCallbacks = true;
	fixture.session->acquire(first.lease);
	fixture.session->acquire(queued.lease);
	Require(queued.callbacks.size() == 1, "queued waiting callback was not dispatched through runner");
	Require(queued.waiting.empty(), "queued waiting bypassed runner");
	fixture.session->release(queued.lease, false);
	queued.drain();
	Require(queued.waiting.empty(), "cancelled lease received queued waiting notification");
	fixture.session->release(first.lease, true);
	Require(!queued.id, "cancelled waiting lease was promoted");
	Require(fixture.server.initializations.size() == 1, "cancelled waiting lease initialized");
}

void WaitingCallbackCanCancelBeforeReady() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	first.lease->waiting = [&](bool waiting) {
		Require(!waiting, "first lease unexpectedly queued");
		fixture.session->release(first.lease, false);
	};
	fixture.session->acquire(first.lease);
	Require(!first.id, "waiting callback cancellation delivered ready");
	Require(!fixture.server.active, "waiting callback cancellation leaked takeout");
	Require(fixture.server.finishes.size() == 1, "cancelled initialization was not cleaned up");
}

void WaitingCallbackCanCancelCompatibleReadyLease() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto joined = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	joined.lease->waiting = [&](bool waiting) {
		Require(!waiting, "compatible ready lease unexpectedly queued");
		fixture.session->release(joined.lease, false);
	};
	fixture.session->acquire(joined.lease);
	Require(!joined.id, "waiting callback cancellation still delivered ready");
	Require(fixture.server.finishes.empty(), "joined waiting cancellation finished survivor");
	fixture.requireValid(first);
}

void FreshAccountGetsFreshCoordinatorAndCallbacksDieWithOwner() {
	auto server = MTP::Server();
	auto firstOwner = std::make_unique<Main::Session>(server);
	const auto firstSession = Session::ForSession(firstOwner.get());
	Require(Session::ForSession(firstOwner.get()).get() == firstSession.get(),
		"same account did not reuse its coordinator");
	auto first = Job(Flag::f_message_channels, 0);
	first.deferCallbacks = true;
	firstSession->acquire(first.lease);
	firstOwner.reset();
	first.drain();
	Require(!firstSession, "coordinator outlived owning account session");
	Require(!first.id, "owner destruction delivered stale ready callback");
	Require(first.waiting.empty(), "owner destruction delivered stale waiting callback");
	Require(!server.active, "owner destruction left an active takeout");
	auto secondOwner = Main::Session(server);
	const auto secondSession = Session::ForSession(&secondOwner);
	auto second = Job(Flag::f_contacts, 0);
	secondSession->acquire(second.lease);
	Require(second.id.has_value(), "fresh account could not start export");
	Require(server.initializations.size() == 2, "fresh account reused stale takeout");
}

void OwnerDestructionAfterServerInitBeforeDeliveryCleansTakeout() {
	auto server = MTP::Server();
	auto owner = std::make_unique<Main::Session>(server);
	const auto session = Session::ForSession(owner.get());
	auto job = Job(Flag::f_message_channels, 0);
	crl::deferMain = true;
	session->acquire(job.lease);
	Require(server.active.has_value(), "server initialization was not executed");
	Require(crl::pendingMain.size() == 1, "init response was not queued");
	owner.reset();
	crl::drain();
	crl::deferMain = false;
	Require(!job.id, "destroyed owner started a job after init response");
	Require(!server.active, "owner destruction leaked server-created takeout before init delivery");
}

void ReentrantReadyCanReleaseWithoutInvalidatingOtherLeases() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_message_channels, 0);
	first.lease->ready = [&](uint64) {
		fixture.session->release(first.lease, false);
	};
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	fixture.server.drain();
	fixture.requireValid(second);
	Require(fixture.server.finishes.empty(), "reentrant release finished another lease");
}

void ReentrantReadyCanAcquireWithoutInvalidatingIteration() {
	auto fixture = Fixture();
	fixture.server.defer = true;
	auto first = Job(Flag::f_message_channels, 0);
	auto second = Job(Flag::f_message_channels, 0);
	auto joined = Job(Flag::f_message_channels, 0);
	first.lease->ready = [&](uint64 id) {
		first.id = id;
		fixture.session->acquire(joined.lease);
	};
	fixture.session->acquire(first.lease);
	fixture.session->acquire(second.lease);
	fixture.server.drain();
	fixture.requireValid(first);
	fixture.requireValid(second);
	fixture.requireValid(joined);
	Require(first.id == joined.id, "reentrant acquisition did not join current takeout");
	Require(fixture.server.initializations.size() == 1, "reentrant acquisition initialized again");
}

void OwnerDestructionBeforeNetworkResponseStillCleansReturnedId() {
	auto server = MTP::Server();
	server.defer = true;
	auto owner = std::make_unique<Main::Session>(server);
	const auto session = Session::ForSession(owner.get());
	auto job = Job(Flag::f_message_channels, 0);
	session->acquire(job.lease);
	owner.reset();
	server.drain();
	Require(!job.id, "destroyed owner started after deferred init");
	Require(server.initializations.size() == 1, "pending init did not reach server");
	Require(server.finishes.size() == 1, "returned orphaned id was not finished");
	Require(!server.active, "destroyed pending owner leaked takeout");
}

void MtpDestructionBeforeInitializationDeliveryDropsCallbacks() {
	auto server = std::make_unique<MTP::Server>();
	auto owner = std::make_unique<Main::Session>(*server);
	const auto session = Session::ForSession(owner.get());
	auto job = Job(Flag::f_message_channels, 0);
	crl::deferMain = true;
	session->acquire(job.lease);
	Require(crl::pendingMain.size() == 1, "init response was not queued");
	server.reset();
	crl::drain();
	crl::deferMain = false;
	Require(!job.id && job.errors.empty(), "destroyed transport delivered init response");
	owner.reset();
}

void ClosedMtpRejectsFreshLeaseWithoutDereferencingTransport() {
	auto server = std::make_unique<MTP::Server>();
	auto owner = std::make_unique<Main::Session>(*server);
	const auto session = Session::ForSession(owner.get());
	server.reset();
	auto job = Job(Flag::f_message_channels, 0);
	session->acquire(job.lease);
	Require(!job.id, "closed transport produced takeout id");
	Require(job.errors == std::vector<std::string>{ "EXPORT_SESSION_CLOSED" },
		"closed transport did not produce a clear local error");
}

void NewJobWaitsWhileLastFinishIsInFlight() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto next = Job(Flag::f_message_channels, 0);
	fixture.session->acquire(first.lease);
	fixture.server.defer = true;
	auto completed = false;
	fixture.session->release(first.lease, true, [&] { completed = true; });
	fixture.session->acquire(next.lease);
	Require(!next.id, "new job reused a takeout that was finishing");
	Require(fixture.server.initializations.size() == 1, "new init overtook in-flight finish");
	fixture.server.drain();
	Require(completed, "last completion was not delivered");
	fixture.requireValid(next);
	Require(next.id != first.id, "new group reused the finished takeout id");
	Require(fixture.server.finishes.size() == 1, "prior group did not finish once");
}

void FinishFailureReleasesQueueAndReportsFailure() {
	auto fixture = Fixture();
	auto first = Job(Flag::f_message_channels, 0);
	auto next = Job(Flag::f_contacts, 0);
	fixture.session->acquire(first.lease);
	fixture.session->acquire(next.lease);
	fixture.server.failNextFinish = MTP::Error{ "FINISH_FAILED" };
	auto completed = false;
	fixture.session->release(first.lease, true, [&] { completed = true; });
	Require(!completed, "failed finish reported completion");
	Require(first.errors == std::vector<std::string>{ "FINISH_FAILED" },
		"failed finish was not reported");
	fixture.requireValid(next);
	Require(fixture.server.initializations.size() == 2, "finish failure blocked queued export");
}

void InitializationFloodAndServerErrorsAreTerminal() {
	for (const auto &error : std::vector<MTP::Error>{
		{ "FLOOD_WAIT_30", 400 },
		{ "INTERNAL_SERVER_ERROR", 500 },
		{ "LOCAL_TRANSPORT_ERROR", -1 },
	}) {
		auto fixture = Fixture();
		fixture.server.defer = true;
		fixture.server.failNextInit = error;
		auto first = Job(Flag::f_message_channels, 0);
		auto joined = Job(Flag::f_message_channels, 0);
		auto queued = Job(Flag::f_contacts, 0);
		fixture.session->acquire(first.lease);
		fixture.session->acquire(joined.lease);
		fixture.session->acquire(queued.lease);
		fixture.server.drain();
		Require(first.errors == std::vector<std::string>{ error.name },
			"default-handled initialization error was not terminal");
		Require(joined.errors == first.errors, "terminal init error did not reach joined job");
		Require(fixture.server.suppressedDefaultErrors == 0,
			"initialization error entered default hidden retry handling");
		fixture.requireValid(queued);
	}
}

void OwnerDestructionBeforeTerminalInitErrorDeliverySuppressesCallbacks() {
	auto server = MTP::Server();
	auto owner = std::make_unique<Main::Session>(server);
	const auto session = Session::ForSession(owner.get());
	auto job = Job(Flag::f_message_channels, 0);
	server.failNextInit = MTP::Error{ "FLOOD_WAIT_30", 400 };
	crl::deferMain = true;
	session->acquire(job.lease);
	Require(crl::pendingMain.size() == 1, "terminal init error callback was not queued");
	owner.reset();
	crl::drain();
	crl::deferMain = false;
	Require(!job.id && job.errors.empty(), "destroyed owner received init error");
	Require(server.initializations.size() == 1, "destroyed owner retried initialization");
	Require(server.suppressedDefaultErrors == 0, "orphan init used default retry handling");
}

}

int main(int argc, char **argv) {
	struct Test {
		const char *name;
		Fn<void()> run;
	};
	auto passed = 0;
	auto total = 0;
	for (const auto &test : std::vector<Test>{
		{ "parallel_compatible_jobs_use_one_takeout", ParallelCompatibleJobsUseOneTakeout },
		{ "individual_completion_keeps_survivor", IndividualCompletionKeepsSurvivorAndLastCompletes },
		{ "incompatible_permission_waits", IncompatiblePermissionWaitsForExistingGroup },
		{ "file_limit_compatibility", LargerFileLimitWaitsAndSmallerLimitShares },
		{ "pending_initializations_join", ConcurrentPendingInitializationsJoinOneRequest },
		{ "initialization_failure_fans_out_and_retries", InitializationFailureFansOutAndQueuedJobRetries },
		{ "queued_cancellation_is_independent", QueuedCancellationCannotStartOrAffectActiveJob },
		{ "pending_cancellation_cleans_returned_session", PendingCancellationCleansReturnedSession },
		{ "late_ready_callback_cannot_revive_job", CancelledReadyCallbackCannotReviveJob },
		{ "queued_waiting_clears_before_initialization", QueuedWaitingClearsBeforeInitializationCompletes },
		{ "compatible_leases_never_report_waiting", CompatiblePendingAndReadyLeasesNeverReportWaiting },
		{ "cancelled_waiting_notification_is_suppressed", CancelledQueuedWaitingNotificationIsSuppressed },
		{ "waiting_callback_can_cancel_before_ready", WaitingCallbackCanCancelBeforeReady },
		{ "waiting_callback_can_cancel_compatible_ready", WaitingCallbackCanCancelCompatibleReadyLease },
		{ "fresh_account_and_owner_lifetime", FreshAccountGetsFreshCoordinatorAndCallbacksDieWithOwner },
		{ "owner_destruction_before_init_delivery", OwnerDestructionAfterServerInitBeforeDeliveryCleansTakeout },
		{ "reentrant_ready_release", ReentrantReadyCanReleaseWithoutInvalidatingOtherLeases },
		{ "reentrant_ready_acquire", ReentrantReadyCanAcquireWithoutInvalidatingIteration },
		{ "owner_destruction_before_network_response", OwnerDestructionBeforeNetworkResponseStillCleansReturnedId },
		{ "mtp_destruction_before_init_delivery", MtpDestructionBeforeInitializationDeliveryDropsCallbacks },
		{ "closed_mtp_rejects_fresh_lease", ClosedMtpRejectsFreshLeaseWithoutDereferencingTransport },
		{ "new_job_waits_for_inflight_finish", NewJobWaitsWhileLastFinishIsInFlight },
		{ "finish_failure_releases_queue", FinishFailureReleasesQueueAndReportsFailure },
		{ "init_flood_and_server_errors_are_terminal", InitializationFloodAndServerErrorsAreTerminal },
		{ "owner_destruction_before_terminal_init_error", OwnerDestructionBeforeTerminalInitErrorDeliverySuppressesCallbacks },
	}) {
		if (argc == 3 && std::string(argv[1]) == "--case"
			&& test.name != std::string(argv[2])) {
			continue;
		}
		++total;
		try {
			test.run();
			++passed;
			std::cout << "PASS " << test.name << '\n';
		} catch (const std::exception &error) {
			std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
		}
		crl::deferMain = false;
		crl::drain();
	}
	std::cout << passed << '/' << total << " takeout session tests passed\n";
	return passed == total && total != 0 ? 0 : 1;
}
