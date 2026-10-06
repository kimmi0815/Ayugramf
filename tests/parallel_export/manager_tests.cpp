#include "export/export_manager.h"
#include "fake_domain.h"

namespace {

using Export::Controller;
using Export::View::PanelController;
using Jobs = std::vector<Export::JobInfo>;

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

Controller *GetController(Main::Session &session, uint64 peerId, int32 topic = 0) {
	const auto controller = Controller::Find(&session, peerId, topic);
	Require(controller != nullptr, "expected controller must exist");
	return controller;
}

PanelController *GetPanel(Main::Session &session, uint64 peerId, int32 topic = 0) {
	const auto panel = PanelController::Find(GetController(session, peerId, topic));
	Require(panel != nullptr, "expected panel must exist");
	return panel;
}

Jobs Snapshot(Export::Manager &manager, Main::Session &session) {
	auto result = Jobs();
	auto lifetime = rpl::lifetime();
	auto calls = 0;
	manager.jobs(&session) | rpl::on_next([&](Jobs jobs) {
		result = std::move(jobs);
		++calls;
	}, lifetime);
	Require(calls == 1, "snapshot observer must receive current jobs once");
	return result;
}

const Export::JobInfo &JobById(const Jobs &jobs, uint64 id) {
	const auto i = ranges::find_if(jobs, [=](const auto &job) {
		return job.id == id;
	});
	Require(i != jobs.end(), "expected job ID must be present");
	return *i;
}

const Export::JobInfo &JobByTitle(const Jobs &jobs, const QString &title) {
	const auto i = ranges::find_if(jobs, [&](const auto &job) {
		return job.title == title;
	});
	Require(i != jobs.end(), "expected job title must be present");
	return *i;
}

void CheckNoJobs() {
	Require(Controller::Count() == 0, "controllers leaked between tests");
	Require(PanelController::Count() == 0, "panels leaked between tests");
}

void ChatAccountTopicIdentity() {
	auto firstSession = Main::Session();
	auto secondSession = Main::Session();
	auto firstPeer = PeerData(&firstSession, 1);
	auto secondPeer = PeerData(&firstSession, 2);
	auto otherAccountPeer = PeerData(&secondSession, 1);
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	manager.start(&otherAccountPeer);
	manager.startTopic(&firstPeer, { 10 }, "first topic");
	manager.startTopic(&firstPeer, { 11 }, "second topic");
	manager.start(&firstSession);
	Require(Controller::Count() == 6, "chat, topic and account keys must be independent");
	const auto firstPanel = GetPanel(firstSession, 1);
	const auto topicPanel = GetPanel(firstSession, 1, 10);
	manager.start(&firstPeer);
	manager.startTopic(&firstPeer, { 10 }, "updated title");
	manager.start(&firstSession);
	Require(Controller::Count() == 6, "same-key starts must reuse controllers");
	Require(firstPanel->activations() == 1, "same chat must activate its panel");
	Require(topicPanel->activations() == 1, "same topic must activate its panel");
	Require(manager.inProgress(&firstSession), "first account must own jobs");
	Require(manager.inProgress(&secondSession), "second account must own jobs");
}

void CancelOneKeepsOthersAndIgnoresLateEvents() {
	auto session = Main::Session();
	auto firstPeer = PeerData(&session, 1);
	auto secondPeer = PeerData(&session, 2);
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	const auto removedSource = GetController(session, 1)->source();
	GetPanel(session, 1)->requestStop();
	Require(Controller::Find(&session, 1) == nullptr, "only cancelled controller must be removed");
	Require(Controller::Count() == 1, "other controller must survive cancellation");
	Require(GetPanel(session, 2) != nullptr, "other panel must survive cancellation");
	removedSource->changes.fire(Export::FinishedState());
	removedSource->changes.fire(Export::ProcessingState());
	Require(Controller::Count() == 1, "late state must not revive cancelled job");
	manager.stop();
	Require(!manager.inProgress(), "stop must remove every job");
}

void ProcessingSelectionAndCompletion() {
	auto session = Main::Session();
	auto firstPeer = PeerData(&session, 1);
	auto secondPeer = PeerData(&session, 2);
	auto manager = Export::Manager();
	auto observed = std::vector<PanelController*>();
	auto lifetime = rpl::lifetime();
	manager.currentView(&session) | rpl::on_next([&](PanelController *panel) {
		observed.push_back(panel);
	}, lifetime);
	Require(observed.back() == nullptr, "empty manager must expose no progress panel");
	manager.start(&firstPeer);
	Require(observed.back() == nullptr, "settings-only panel must expose no progress");
	GetController(session, 1)->setState(Export::ProcessingState());
	const auto firstPanel = GetPanel(session, 1);
	Require(observed.back() == firstPanel, "processing job must become current");
	manager.start(&secondPeer);
	Require(observed.back() == firstPanel, "new settings panel must not replace running progress");
	GetController(session, 2)->setState(Export::ProcessingState());
	Require(observed.back() == GetPanel(session, 2), "newest processing job must become current");
	GetController(session, 2)->setState(Export::FinishedState());
	Require(observed.back() == firstPanel, "completion must reveal older running export");
	GetController(session, 1)->setState(Export::FinishedState());
	Require(observed.back() == nullptr, "no processing jobs must clear current progress");
	Require(manager.inProgress(), "completed panels remain available for output review");
}

void ErrorAndCancellationClearProgress() {
	auto session = Main::Session();
	auto peer = PeerData(&session, 1);
	auto manager = Export::Manager();
	auto current = static_cast<PanelController*>(nullptr);
	auto lifetime = rpl::lifetime();
	manager.currentView(&session) | rpl::on_next([&](PanelController *panel) {
		current = panel;
	}, lifetime);
	manager.start(&peer);
	const auto controller = GetController(session, 1);
	controller->setState(Export::ProcessingState());
	Require(current != nullptr, "processing must expose panel");
	controller->setState(Export::ApiErrorState());
	Require(current == nullptr, "API error must clear progress");
	controller->setState(Export::ProcessingState());
	controller->setState(Export::OutputErrorState());
	Require(current == nullptr, "output error must clear progress");
	controller->setState(Export::ProcessingState());
	controller->setState(Export::CancelledState());
	Require(current == nullptr, "cancelled state must clear progress");
}

void AccountReplacementRemovesOnlyOldSession() {
	auto sharedAccount = Main::Account();
	auto oldSession = Main::Session(&sharedAccount);
	auto newSession = Main::Session(&sharedAccount);
	auto otherSession = Main::Session();
	auto oldFirst = PeerData(&oldSession, 1);
	auto oldSecond = PeerData(&oldSession, 2);
	auto newPeer = PeerData(&newSession, 1);
	auto otherPeer = PeerData(&otherSession, 1);
	auto manager = Export::Manager();
	manager.start(&oldFirst);
	manager.start(&oldSecond);
	manager.start(&otherPeer);
	GetController(oldSession, 1)->setState(Export::ProcessingState());
	GetController(oldSession, 2)->setState(Export::ProcessingState());
	sharedAccount.changeSession(&newSession);
	Require(!manager.inProgress(&oldSession), "session replacement must remove all old jobs");
	Require(manager.inProgress(&otherSession), "another account must survive replacement");
	Require(Controller::Count() == 1, "replacement must leave only unrelated account");
	manager.start(&newPeer);
	sharedAccount.changeSession(&newSession);
	Require(manager.inProgress(&newSession), "unchanged session signal must preserve its jobs");
	sharedAccount.changeSession(nullptr);
	Require(!manager.inProgress(&newSession), "logout must remove replacement session jobs");
	Require(manager.inProgress(&otherSession), "logout must preserve unrelated account");
}

void ScopedLogoutConfirmation() {
	auto session = Main::Session();
	auto otherSession = Main::Session();
	auto firstPeer = PeerData(&session, 1);
	auto secondPeer = PeerData(&session, 2);
	auto otherPeer = PeerData(&otherSession, 1);
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	manager.start(&otherPeer);
	GetController(session, 1)->setState(Export::ProcessingState());
	GetController(session, 2)->setState(Export::ProcessingState());
	GetController(otherSession, 1)->setState(Export::ProcessingState());
	auto callbacks = 0;
	manager.stopWithConfirmation([&] { ++callbacks; }, &session);
	Require(GetPanel(session, 2)->awaitingConfirmation(), "scoped stop must ask newest job");
	Require(!GetPanel(otherSession, 1)->awaitingConfirmation(), "unrelated account must not be asked");
	GetPanel(session, 2)->confirmStop();
	Require(callbacks == 0, "callback must await all account jobs");
	Require(GetPanel(session, 1)->awaitingConfirmation(), "scoped stop must ask next account job");
	GetPanel(session, 1)->confirmStop();
	Require(callbacks == 1, "scoped completion callback must fire once");
	Require(!manager.inProgress(&session), "scoped stop must remove account jobs");
	Require(manager.inProgress(&otherSession), "scoped stop must preserve other account");
}

void QuitConfirmationAndSettingsPanels() {
	auto firstSession = Main::Session();
	auto secondSession = Main::Session();
	auto firstPeer = PeerData(&firstSession, 1);
	auto settingsPeer = PeerData(&firstSession, 2);
	auto otherPeer = PeerData(&secondSession, 1);
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&settingsPeer);
	manager.start(&otherPeer);
	GetController(firstSession, 1)->setState(Export::ProcessingState());
	GetController(secondSession, 1)->setState(Export::ProcessingState());
	auto callbacks = 0;
	manager.stopWithConfirmation([&] { ++callbacks; });
	GetPanel(secondSession, 1)->confirmStop();
	Require(callbacks == 0, "quit callback must await other account");
	GetPanel(firstSession, 1)->confirmStop();
	Require(callbacks == 1, "quit callback must fire exactly once after all jobs");
	Require(!manager.inProgress(), "quit confirmation must remove settings and running panels");
	CheckNoJobs();
}

void ConfirmationCallbackLifetime() {
	auto session = Main::Session();
	auto peer = PeerData(&session, 1);
	auto manager = Export::Manager();
	manager.start(&peer);
	GetController(session, 1)->setState(Export::ProcessingState());
	auto callbacks = 0;
	auto marker = std::make_shared<int>(1);
	auto weakMarker = std::weak_ptr<int>(marker);
	manager.stopWithConfirmation([marker, &callbacks] { ++callbacks; }, &session);
	marker.reset();
	Require(!weakMarker.expired(), "pending confirmation must retain callback");
	session.account().changeSession(nullptr);
	Require(weakMarker.expired(), "session removal must destroy pending callback");
	Require(callbacks == 0, "destroyed confirmation must not invoke callback");
	manager.stopWithConfirmation([&] { ++callbacks; }, &session);
	Require(callbacks == 1, "empty scoped stop must complete once");
}

void RepeatedObserversAndLifetimeDestruction() {
	auto session = Main::Session();
	auto peer = PeerData(&session, 1);
	auto manager = Export::Manager();
	manager.start(&peer);
	const auto controller = GetController(session, 1);
	auto firstLifetime = rpl::lifetime();
	auto firstCalls = 0;
	manager.currentView(&session) | rpl::on_next([&](PanelController*) {
		++firstCalls;
	}, firstLifetime);
	controller->setState(Export::ProcessingState());
	Require(firstCalls == 2, "observer must see initial null and running panel");
	firstLifetime.destroy();
	const auto source = controller->source();
	for (auto i = 0; i != 100; ++i) {
		auto lifetime = rpl::lifetime();
		auto calls = 0;
		manager.currentView(&session) | rpl::on_next([&](PanelController*) {
			++calls;
		}, lifetime);
		Require(calls == 1, "replacement observer must see current state once");
		lifetime.destroy();
		controller->setState(Export::FinishedState());
		controller->setState(Export::ProcessingState());
		Require(calls == 1, "destroyed observer must not receive later state");
	}
	manager.stop();
	source->changes.fire(Export::ProcessingState());
	Require(firstCalls == 2, "old observer must stay disconnected after stop");
	Require(!source->changes.has_consumers(), "removed job must release all state subscriptions");
}

void RunningJobsSnapshotAndIndependentProgress() {
	auto session = Main::Session();
	auto firstPeer = PeerData(&session, 1, "First chat");
	auto secondPeer = PeerData(&session, 2, "Second chat");
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	auto first = Export::ProcessingState();
	first.step = Export::ProcessingState::Step::Dialogs;
	first.entityType = Export::ProcessingState::EntityType::Chat;
	first.entityName = "First chat";
	first.entityIndex = 1;
	first.entityCount = 2;
	first.itemIndex = 12;
	first.itemCount = 80;
	first.bytesRandomId = 101;
	first.bytesName = "first.mp4";
	first.bytesLoaded = 120;
	first.bytesCount = 1000;
	first.outputPath = "/exports/first";
	auto second = Export::ProcessingState();
	second.step = Export::ProcessingState::Step::Dialogs;
	second.itemIndex = 5;
	second.itemCount = 60;
	second.bytesLoaded = 25;
	second.bytesCount = 800;
	second.waitingForTakeout = true;
	GetController(session, 1)->setState(first);
	GetController(session, 2)->setState(second);
	auto snapshots = std::vector<Jobs>();
	auto lifetime = rpl::lifetime();
	manager.jobs(&session) | rpl::on_next([&](Jobs jobs) {
		snapshots.push_back(std::move(jobs));
	}, lifetime);
	Require(snapshots.size() == 1, "already-running jobs must emit one initial snapshot");
	Require(snapshots.back().size() == 2, "initial snapshot must include both running jobs");
	const auto firstId = JobByTitle(snapshots.back(), "First chat").id;
	const auto secondId = JobByTitle(snapshots.back(), "Second chat").id;
	Require(firstId != 0 && secondId != 0 && firstId != secondId, "each running job must have a distinct stable ID");
	const auto &initial = std::get<Export::ProcessingState>(
		JobById(snapshots.back(), firstId).state);
	Require(initial.itemIndex == 12 && initial.itemCount == 80, "snapshot must retain latest item progress");
	Require(initial.entityName == "First chat" && initial.entityIndex == 1 && initial.entityCount == 2, "snapshot must retain entity progress");
	Require(initial.bytesRandomId == 101 && initial.bytesName == "first.mp4", "snapshot must retain file identity");
	Require(initial.bytesLoaded == 120 && initial.bytesCount == 1000, "snapshot must retain latest byte progress");
	Require(initial.outputPath == "/exports/first", "snapshot must retain output path");
	first.bytesLoaded = 240;
	GetController(session, 1)->setState(first);
	Require(snapshots.size() == 2, "byte update without a state transition must emit a snapshot");
	Require(std::get<Export::ProcessingState>(JobById(snapshots.back(), firstId).state).bytesLoaded == 240, "first job byte progress must update");
	Require(std::get<Export::ProcessingState>(JobById(snapshots.back(), secondId).state).bytesLoaded == 25, "first update must preserve second job byte progress");
	second.itemIndex = 6;
	second.waitingForTakeout = false;
	GetController(session, 2)->setState(second);
	Require(snapshots.size() == 3, "item update without a state transition must emit a snapshot");
	Require(std::get<Export::ProcessingState>(JobById(snapshots.back(), secondId).state).itemIndex == 6, "second job item progress must update");
	Require(!std::get<Export::ProcessingState>(JobById(snapshots.back(), secondId).state).waitingForTakeout, "takeout waiting flag must update in the row");
	Require(std::get<Export::ProcessingState>(JobById(snapshots.back(), firstId).state).itemIndex == 12, "second update must preserve first job item progress");
	Require(std::get<Export::ProcessingState>(JobById(snapshots.front(), firstId).state).bytesLoaded == 120, "delivered snapshots must remain independent copies");
}

void AccountScopedSnapshotsAndSettingsRows() {
	auto firstSession = Main::Session();
	auto secondSession = Main::Session();
	auto firstPeer = PeerData(&firstSession, 1, "First account chat");
	auto secondPeer = PeerData(&firstSession, 2, "Second account chat");
	auto settingsPeer = PeerData(&firstSession, 3, "Settings chat");
	auto otherPeer = PeerData(&secondSession, 1, "Other account chat");
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	manager.start(&otherPeer);
	auto progress = Export::ProcessingState();
	progress.itemIndex = 7;
	progress.itemCount = 100;
	GetController(firstSession, 1)->setState(progress);
	GetController(firstSession, 2)->setState(progress);
	GetController(secondSession, 1)->setState(progress);
	auto firstJobs = Jobs();
	auto secondJobs = Jobs();
	auto lifetime = rpl::lifetime();
	manager.jobs(&firstSession) | rpl::on_next([&](Jobs jobs) {
		firstJobs = std::move(jobs);
	}, lifetime);
	manager.jobs(&secondSession) | rpl::on_next([&](Jobs jobs) {
		secondJobs = std::move(jobs);
	}, lifetime);
	Require(firstJobs.size() == 2 && secondJobs.size() == 1, "each account snapshot must include only its own jobs");
	const auto firstId = JobByTitle(firstJobs, "First account chat").id;
	const auto secondId = JobByTitle(firstJobs, "Second account chat").id;
	const auto otherId = secondJobs.front().id;
	manager.start(&settingsPeer);
	Require(firstJobs.size() == 3, "new settings job must coexist with both running rows");
	Require(v::is<Export::PasswordCheckState>(JobByTitle(firstJobs, "Settings chat").state), "settings row must expose its own state");
	Require(v::is<Export::ProcessingState>(JobById(firstJobs, firstId).state), "settings row must preserve first running row");
	Require(v::is<Export::ProcessingState>(JobById(firstJobs, secondId).state), "settings row must preserve second running row");
	Require(secondJobs.size() == 1 && secondJobs.front().id == otherId, "opening first account settings must preserve second account rows");
	progress.itemIndex = 8;
	GetController(firstSession, 1)->setState(progress);
	Require(std::get<Export::ProcessingState>(JobById(firstJobs, firstId).state).itemIndex == 8, "running row must continue updating while settings are open");
	Require(std::get<Export::ProcessingState>(secondJobs.front().state).itemIndex == 7, "first account update must not change second account progress");
	GetPanel(firstSession, 3)->requestStop();
	Require(firstJobs.size() == 2 && secondJobs.size() == 1, "closing settings must preserve both running rows and other account");
}

void JobTitlesAndStableIdActivation() {
	auto session = Main::Session();
	auto otherSession = Main::Session();
	auto peer = PeerData(&session, 1, "Forum 日本語");
	auto otherPeer = PeerData(&otherSession, 1, "Other account");
	auto manager = Export::Manager();
	manager.start(&peer);
	manager.startTopic(&peer, { 10 }, "Topic α");
	manager.startTopic(&peer, { 11 }, "Topic β");
	manager.start(&session);
	manager.start(&otherPeer);
	const auto jobs = Snapshot(manager, session);
	Require(jobs.size() == 4, "chat, topics and full-account export must have separate rows");
	const auto chatId = JobByTitle(jobs, "Forum 日本語").id;
	const auto firstTopicId = JobByTitle(jobs, "Forum 日本語 / Topic α").id;
	const auto secondTopicId = JobByTitle(jobs, "Forum 日本語 / Topic β").id;
	const auto accountId = JobByTitle(jobs, "").id;
	const auto chatPanel = GetPanel(session, 1);
	const auto firstTopicPanel = GetPanel(session, 1, 10);
	const auto secondTopicPanel = GetPanel(session, 1, 11);
	const auto accountPanel = GetPanel(session, 0);
	const auto otherPanel = GetPanel(otherSession, 1);
	manager.activate(secondTopicId, &session);
	Require(secondTopicPanel->activations() == 1, "topic ID must activate its own panel");
	Require(chatPanel->activations() == 0 && firstTopicPanel->activations() == 0 && accountPanel->activations() == 0, "topic activation must leave other panels unchanged");
	manager.activate(firstTopicId, &otherSession);
	Require(firstTopicPanel->activations() == 0 && otherPanel->activations() == 0, "activation from another account must be ignored");
	manager.startTopic(&peer, { 10 }, "Later topic title");
	Require(JobById(Snapshot(manager, session), firstTopicId).title == "Forum 日本語 / Topic α", "same-key activation must retain the job title and ID");
	chatPanel->requestStop();
	manager.activate(secondTopicId, &session);
	manager.activate(accountId, &session);
	Require(secondTopicPanel->activations() == 2, "row removal must not shift the meaning of later IDs");
	Require(accountPanel->activations() == 1, "account export ID must activate its own panel");
	manager.activate(chatId, &session);
	Require(secondTopicPanel->activations() == 2 && accountPanel->activations() == 1, "removed ID must not activate a remaining row");
}

void TerminalRowsRemainSeparateAndVisible() {
	auto session = Main::Session();
	auto firstPeer = PeerData(&session, 1, "Finished chat");
	auto secondPeer = PeerData(&session, 2, "API error chat");
	auto thirdPeer = PeerData(&session, 3, "Output error chat");
	auto fourthPeer = PeerData(&session, 4, "Running chat");
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	manager.start(&thirdPeer);
	manager.start(&fourthPeer);
	auto jobs = Jobs();
	auto lifetime = rpl::lifetime();
	manager.jobs(&session) | rpl::on_next([&](Jobs value) {
		jobs = std::move(value);
	}, lifetime);
	const auto finishedId = JobByTitle(jobs, "Finished chat").id;
	const auto apiErrorId = JobByTitle(jobs, "API error chat").id;
	const auto outputErrorId = JobByTitle(jobs, "Output error chat").id;
	const auto runningId = JobByTitle(jobs, "Running chat").id;
	auto progress = Export::ProcessingState();
	progress.itemIndex = 41;
	progress.itemCount = 50;
	for (auto peerId = uint64(1); peerId != 5; ++peerId) {
		GetController(session, peerId)->setState(progress);
	}
	GetController(session, 1)->setState(Export::FinishedState{
		"/exports/finished",
		9,
		12345,
	});
	GetController(session, 2)->setState(Export::ApiErrorState());
	GetController(session, 3)->setState(Export::OutputErrorState{
		"/exports/blocked",
	});
	Require(jobs.size() == 4, "finished and error rows must remain visible alongside running row");
	const auto &finished = std::get<Export::FinishedState>(JobById(jobs, finishedId).state);
	Require(finished.path == "/exports/finished" && finished.filesCount == 9 && finished.bytesCount == 12345, "finished row must retain its own output details");
	Require(v::is<Export::ApiErrorState>(JobById(jobs, apiErrorId).state), "API error must affect only its own row");
	Require(std::get<Export::OutputErrorState>(JobById(jobs, outputErrorId).state).path == "/exports/blocked", "output error row must retain its own failed path");
	Require(std::get<Export::ProcessingState>(JobById(jobs, runningId).state).itemIndex == 41, "terminal rows must preserve unrelated running progress");
	GetController(session, 2)->setState(Export::CancelledState());
	Require(v::is<Export::CancelledState>(JobById(jobs, apiErrorId).state), "cancelled state must remain visible for that stable ID");
	manager.activate(finishedId, &session);
	manager.activate(outputErrorId, &session);
	Require(GetPanel(session, 1)->activations() == 1 && GetPanel(session, 3)->activations() == 1, "finished and error IDs must remain activatable");
}

void RemovedAndReplacedIdsCannotActivate() {
	auto account = Main::Account();
	auto oldSession = Main::Session(&account);
	auto newSession = Main::Session(&account);
	auto otherSession = Main::Session();
	auto oldPeer = PeerData(&oldSession, 1, "Old chat");
	auto newPeer = PeerData(&newSession, 1, "New chat");
	auto otherPeer = PeerData(&otherSession, 1, "Other chat");
	auto manager = Export::Manager();
	manager.start(&oldPeer);
	manager.start(&otherPeer);
	const auto removedId = Snapshot(manager, oldSession).front().id;
	GetPanel(oldSession, 1)->requestStop();
	manager.start(&oldPeer);
	const auto replacementId = Snapshot(manager, oldSession).front().id;
	Require(replacementId != removedId, "restarted chat must receive a fresh stable ID");
	const auto restartedPanel = GetPanel(oldSession, 1);
	const auto otherPanel = GetPanel(otherSession, 1);
	manager.activate(removedId, &oldSession);
	manager.activate(replacementId, &otherSession);
	Require(restartedPanel->activations() == 0 && otherPanel->activations() == 0, "removed and mismatched-account IDs must be no-ops");
	auto oldJobs = Jobs();
	auto lifetime = rpl::lifetime();
	manager.jobs(&oldSession) | rpl::on_next([&](Jobs jobs) {
		oldJobs = std::move(jobs);
	}, lifetime);
	account.changeSession(&newSession);
	Require(oldJobs.empty(), "session replacement must publish an empty old-account snapshot");
	manager.start(&newPeer);
	const auto newId = Snapshot(manager, newSession).front().id;
	const auto newPanel = GetPanel(newSession, 1);
	manager.activate(replacementId, &oldSession);
	manager.activate(replacementId, &newSession);
	manager.activate(newId, &oldSession);
	Require(newPanel->activations() == 0 && otherPanel->activations() == 0, "stale replaced-session IDs must not activate new or unrelated panels");
	manager.activate(newId, &newSession);
	Require(newPanel->activations() == 1, "replacement account's current ID must still activate");
	account.changeSession(nullptr);
	manager.activate(newId, &newSession);
	Require(Snapshot(manager, newSession).empty(), "logout must remove the replacement session's rows");
	Require(otherPanel->activations() == 0, "logged-out ID must not activate surviving account");
}

void ReentrantSnapshotRemovalAndLateState() {
	auto session = Main::Session();
	auto firstPeer = PeerData(&session, 1, "Removed chat");
	auto secondPeer = PeerData(&session, 2, "Surviving chat");
	auto manager = Export::Manager();
	manager.start(&firstPeer);
	manager.start(&secondPeer);
	const auto initial = Snapshot(manager, session);
	const auto removedId = JobByTitle(initial, "Removed chat").id;
	const auto survivingId = JobByTitle(initial, "Surviving chat").id;
	const auto source = GetController(session, 1)->source();
	auto removed = false;
	auto latest = Jobs();
	auto lifetime = rpl::lifetime();
	manager.jobs(&session) | rpl::on_next([&](const Jobs &jobs) {
		const auto i = ranges::find_if(jobs, [=](const auto &job) {
			return job.id == removedId
				&& v::is<Export::ProcessingState>(job.state);
		});
		if (!removed && i != jobs.end()) {
			removed = true;
			GetPanel(session, 1)->requestStop();
		}
	}, lifetime);
	manager.jobs(&session) | rpl::on_next([&](Jobs jobs) {
		latest = std::move(jobs);
	}, lifetime);
	auto progress = Export::ProcessingState();
	progress.itemIndex = 17;
	GetController(session, 1)->setState(progress);
	Require(removed && latest.size() == 1 && latest.front().id == survivingId, "reentrant row removal must publish only the surviving job");
	source->changes.fire(Export::FinishedState());
	Require(latest.size() == 1 && latest.front().id == survivingId, "late state must not restore reentrantly removed row");
	Require(!source->changes.has_consumers(), "late state must prune disconnected subscriptions after reentrant removal");
	manager.activate(removedId, &session);
	Require(GetPanel(session, 2)->activations() == 0, "reentrantly removed ID must not activate surviving row");
	auto stopLifetime = rpl::lifetime();
	manager.currentView(&session) | rpl::on_next([&](PanelController *panel) {
		if (panel) {
			manager.stop();
		}
	}, stopLifetime);
	GetController(session, 2)->setState(progress);
	Require(latest.empty(), "removal from current-view notification must leave the jobs snapshot empty");
	CheckNoJobs();
}

void JobsObserverLifetimeAndManagerDestruction() {
	auto session = Main::Session();
	auto peer = PeerData(&session, 1, "Observed chat");
	auto manager = std::make_unique<Export::Manager>();
	manager->start(&peer);
	const auto controller = GetController(session, 1);
	const auto source = controller->source();
	auto progress = Export::ProcessingState();
	controller->setState(progress);
	for (auto i = 0; i != 100; ++i) {
		auto lifetime = rpl::lifetime();
		auto calls = 0;
		manager->jobs(&session) | rpl::on_next([&](const Jobs &jobs) {
			++calls;
			Require(jobs.size() == 1, "replacement observer must see current running row");
		}, lifetime);
		Require(calls == 1, "replacement jobs observer must receive initial snapshot once");
		lifetime.destroy();
		++progress.itemIndex;
		controller->setState(progress);
		Require(calls == 1, "destroyed jobs observer must not receive later progress");
	}
	auto selfLifetime = rpl::lifetime();
	auto selfCalls = 0;
	manager->jobs(&session) | rpl::on_next([&](const Jobs&) {
		if (++selfCalls == 2) {
			selfLifetime.destroy();
		}
	}, selfLifetime);
	++progress.itemIndex;
	controller->setState(progress);
	++progress.itemIndex;
	controller->setState(progress);
	Require(selfCalls == 2, "jobs observer must be able to disconnect during delivery");
	auto marker = std::make_shared<int>(1);
	auto weakMarker = std::weak_ptr<int>(marker);
	auto lifetime = rpl::lifetime();
	auto completed = 0;
	auto calls = 0;
	manager->jobs(&session) | rpl::on_next_done([marker, &calls](const Jobs&) {
		++calls;
		Require(*marker == 1, "live jobs observer must retain its captures");
	}, [&] {
		++completed;
	}, lifetime);
	marker.reset();
	Require(!weakMarker.expired(), "manager event stream must retain live jobs observer");
	manager.reset();
	Require(completed == 1, "manager destruction must complete its jobs observer once");
	source->changes.fire(progress);
	Require(calls == 1 && completed == 1, "late source state must not reach completed jobs observer");
	Require(!source->changes.has_consumers(), "late state must prune disconnected subscriptions after manager destruction");
	lifetime.destroy();
	Require(weakMarker.expired(), "caller lifetime destruction must release completed observer captures");
	CheckNoJobs();
}

}

int main() {
	const auto tests = std::vector<std::pair<const char*, Fn<void()>>>{
		{ "chat_account_topic_identity", ChatAccountTopicIdentity },
		{ "cancel_one_and_late_events", CancelOneKeepsOthersAndIgnoresLateEvents },
		{ "processing_selection_and_completion", ProcessingSelectionAndCompletion },
		{ "error_and_cancellation_clear_progress", ErrorAndCancellationClearProgress },
		{ "account_session_replacement", AccountReplacementRemovesOnlyOldSession },
		{ "scoped_logout_confirmation", ScopedLogoutConfirmation },
		{ "quit_confirmation_and_settings", QuitConfirmationAndSettingsPanels },
		{ "confirmation_callback_lifetime", ConfirmationCallbackLifetime },
		{ "repeated_observer_lifetime", RepeatedObserversAndLifetimeDestruction },
		{ "running_jobs_snapshot_and_progress", RunningJobsSnapshotAndIndependentProgress },
		{ "account_scoped_snapshots_and_settings", AccountScopedSnapshotsAndSettingsRows },
		{ "job_titles_and_stable_id_activation", JobTitlesAndStableIdActivation },
		{ "terminal_rows_remain_separate", TerminalRowsRemainSeparateAndVisible },
		{ "removed_and_replaced_id_activation", RemovedAndReplacedIdsCannotActivate },
		{ "reentrant_snapshot_removal", ReentrantSnapshotRemovalAndLateState },
		{ "jobs_observer_lifetime", JobsObserverLifetimeAndManagerDestruction },
	};
	auto failed = 0;
	for (const auto &[name, run] : tests) {
		try {
			CheckNoJobs();
			run();
			CheckNoJobs();
			std::cout << "PASS " << name << '\n';
		} catch (const std::exception &error) {
			++failed;
			std::cerr << "FAIL " << name << ": " << error.what() << '\n';
		}
	}
	std::cout << (tests.size() - failed) << '/' << tests.size() << " tests passed\n";
	return failed ? 1 : 0;
}
