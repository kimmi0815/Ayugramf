#include "export/export_manager.h"
#include "fake_domain.h"

namespace {

using Export::Controller;
using Export::View::PanelController;

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
