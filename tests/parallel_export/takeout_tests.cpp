#include "takeout_domain.h"
#include "takeout_api.inc"

#include "takeout_state_types.inc"

namespace Export {

class ControllerObject {
public:
	ControllerObject(ApiWrap &api, const QString &path);
	void setState(State &&state);
	void waitingForTakeoutChanged(bool waiting);
	const State &currentState() const;
	rpl::producer<State> states() const;

private:
	using Step = ProcessingState::Step;
	bool stopped() const;
	ApiWrap &_api;
	Settings _settings;
	State _state = PasswordCheckState();
	rpl::event_stream<State> _stateChanges;
	rpl::lifetime _lifetime;

};

ControllerObject::ControllerObject(ApiWrap &api, const QString &path)
: _api(api) {
	_settings.path = path;
	_api.waitingForTakeout(
	) | rpl::on_next([=](bool waiting) {
		waitingForTakeoutChanged(waiting);
	}, _lifetime);
}

const State &ControllerObject::currentState() const {
	return _state;
}

rpl::producer<State> ControllerObject::states() const {
	return _stateChanges.events_starting_with_copy(_state);
}

bool ApiWrap::probe() {
	auto received = false;
	mainRequest(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done(FnMut<void()>([&] { received = true; })).send();
	return received;
}

void ApiWrap::requestProbe(FnMut<void()> done) {
	mainRequest(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done(std::move(done)).send();
}

}

#include "takeout_state_methods.inc"

namespace {

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void SingleChatCompletesSuccessfully() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto chat = Export::ApiWrap(server, Export::Settings(), &session);
	auto started = false;
	auto finished = false;
	chat.startMainSession([&] { started = true; });
	Require(started && chat.probe(), "single chat failed to begin");
	chat.finishExport([&] { finished = true; });
	Require(finished, "single chat completion callback did not run");
	Require(!chat.takeoutId(), "completed chat retained its takeout id");
	Require(server.finishes.size() == 1, "single chat did not finish once");
	Require(server.finishes.front().second == 1, "completion lost success flag");
	Require(!server.active, "completed takeout remained active");
}

void SingleChatCancelsWithoutSuccess() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto chat = Export::ApiWrap(server, Export::Settings(), &session);
	chat.startMainSession([] {});
	Require(chat.probe(), "single chat failed to begin");
	chat.cancelExportFast();
	Require(server.finishes.size() == 1, "single chat did not cancel once");
	Require(server.finishes.front().second == 0, "cancellation marked success");
	Require(!server.active, "cancelled takeout remained active");
}

void TakeoutPermissionFlagsMatchSettings() {
	using Type = Export::Settings::Type;
	struct Case {
		Type type;
		int flags;
	};
	for (const auto test : std::vector<Case>{
		{ Type::Contacts, 1 },
		{ Type::PersonalChats, 2 },
		{ Type::BotChats, 2 },
		{ Type::PrivateGroups, 12 },
		{ Type::PublicGroups, 8 },
		{ Type::PrivateChannels, 16 },
		{ Type::PublicChannels, 16 },
		{ Type::Userpics, 32 },
		{ Type::Stories, 32 },
	}) {
		auto server = MTP::Server();
		auto session = Export::TakeoutSession(&server);
		auto settings = Export::Settings();
		settings.types = test.type;
		settings.media.types = 0;
		auto chat = Export::ApiWrap(server, settings, &session);
		chat.startMainSession([] {});
		Require(server.initializations.size() == 1, "missing initialization");
		Require(server.initializations.front().flags == test.flags,
			"takeout permissions do not match selected data");
	}
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto settings = Export::Settings();
	settings.types = Type::PrivateChannels;
	settings.media.sizeLimit = 16 * 1024 * 1024;
	auto chat = Export::ApiWrap(server, settings, &session);
	chat.startMainSession([] {});
	Require(server.initializations.front().flags == 48,
		"channel media permission flags are incorrect");
	Require(server.initializations.front().maxSize == settings.media.sizeLimit,
		"selected media size limit was not sent");
}

void StartingSecondChatKeepsFirstChatAlive() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto settings = Export::Settings();
	settings.types = Export::Settings::Type::PrivateChannels;
	auto first = Export::ApiWrap(server, settings, &session);
	auto second = Export::ApiWrap(server, settings, &session);
	auto firstStarted = false;
	auto secondStarted = false;
	first.startMainSession([&] { firstStarted = true; });
	Require(firstStarted && first.probe(), "first chat did not begin");
	second.startMainSession([&] { secondStarted = true; });
	Require(secondStarted && second.probe(), "second chat did not begin");
	Require(first.probe(),
		"first chat stopped after second chat started: "
			+ (first.errors.empty() ? "no response" : first.errors.back())
			+ "; init calls=" + std::to_string(server.initializations.size()));
	Require(first.takeoutId() == second.takeoutId(),
		"parallel chats must use the same valid takeout id");
	Require(server.initializations.size() == 1,
		"parallel chats initialized multiple takeout sessions");
}

void CancellationBeforeSelfResponseCannotInitialize() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	server.defer = true;
	auto chat = Export::ApiWrap(server, Export::Settings(), &session);
	auto started = false;
	chat.startMainSession([&] { started = true; });
	chat.cancelExportFast();
	server.drain();
	Require(!started, "cancelled chat started after self-id response");
	Require(server.initializations.empty(), "cancelled chat initialized takeout");
}

void CancellationBeforeMainAcquireCannotInitialize() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto chat = Export::ApiWrap(server, Export::Settings(), &session);
	auto started = false;
	crl::deferMain = true;
	chat.startMainSession([&] { started = true; });
	chat.cancelExportFast();
	crl::drain();
	crl::deferMain = false;
	Require(!started, "cancelled chat started after queued acquire");
	Require(server.initializations.empty(), "cancelled queued acquire initialized");
}

void CancellationBeforeReadyCannotReviveChat() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto callbacks = std::deque<FnMut<void()>>();
	auto runner = [&](FnMut<void()> callback) {
		callbacks.push_back(std::move(callback));
	};
	auto chat = Export::ApiWrap(server, Export::Settings(), &session, runner);
	auto started = false;
	chat.startMainSession([&] { started = true; });
	Require(callbacks.size() == 1, "self-id response was not queued");
	auto self = std::move(callbacks.front());
	callbacks.pop_front();
	self();
	Require(callbacks.size() == 2, "initializing and ready responses were not queued");
	chat.cancelExportFast();
	while (!callbacks.empty()) {
		auto next = std::move(callbacks.front());
		callbacks.pop_front();
		next();
	}
	Require(!started && !chat.takeoutId(), "late ready revived cancelled chat");
	Require(!server.active, "cancelled ready takeout was not cleaned up");
}

void QueuedApiReportsWaitingAndInitializingBeforeReady() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto firstSettings = Export::Settings();
	firstSettings.types = Export::Settings::Type::PrivateChannels;
	auto queuedSettings = Export::Settings();
	queuedSettings.types = Export::Settings::Type::Contacts;
	auto first = Export::ApiWrap(server, firstSettings, &session);
	auto queued = Export::ApiWrap(server, queuedSettings, &session);
	auto state = Export::ControllerObject(queued, u"/normalized/export_2/"_q);
	state.setState(Export::ProcessingState());
	auto started = false;
	first.startMainSession([] {});
	queued.startMainSession([&] { started = true; });
	const auto waiting = std::get_if<Export::ProcessingState>(&state.currentState());
	Require(waiting && waiting->waitingForTakeout, "queued API did not report waiting to controller");
	Require(waiting->outputPath == u"/normalized/export_2/"_q,
		"waiting update lost normalized output path");
	Require(!started, "queued API started early");
	server.defer = true;
	first.finishExport([] {});
	Require(server.drainOne(), "prior takeout finish was not pending");
	const auto initializing = std::get_if<Export::ProcessingState>(&state.currentState());
	Require(initializing && !initializing->waitingForTakeout,
		"promoted API did not clear controller waiting before initialization response");
	Require(!started, "promoted API received readiness before initialization response");
	server.drain();
	Require(started, "promoted API never received readiness");
}

void CompatibleApiNeverReportsWaiting() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto settings = Export::Settings();
	settings.types = Export::Settings::Type::PrivateChannels;
	auto first = Export::ApiWrap(server, settings, &session);
	auto second = Export::ApiWrap(server, settings, &session);
	auto notifications = std::vector<bool>();
	auto lifetime = rpl::lifetime();
	second.waitingForTakeout(
	) | rpl::on_next([&](bool waiting) {
		notifications.push_back(waiting);
	}, lifetime);
	first.startMainSession([] {});
	second.startMainSession([] {});
	Require(!notifications.empty(), "compatible API did not publish availability");
	Require(std::ranges::none_of(notifications, [](bool value) { return value; }),
		"compatible API reported queue waiting");
	Require(first.probe() && second.probe(), "compatible API lost shared takeout");
}

void CancelledActorDropsPendingWaitingNotifications() {
	for (const auto destroy : { false, true }) {
		auto server = MTP::Server();
		auto session = Export::TakeoutSession(&server);
		auto firstSettings = Export::Settings();
		firstSettings.types = Export::Settings::Type::PrivateChannels;
		auto queuedSettings = Export::Settings();
		queuedSettings.types = Export::Settings::Type::Contacts;
		auto first = Export::ApiWrap(server, firstSettings, &session);
		first.startMainSession([] {});
		auto callbacks = std::deque<FnMut<void()>>();
		auto runner = [&](FnMut<void()> callback) {
			callbacks.push_back(std::move(callback));
		};
		auto queued = std::make_unique<Export::ApiWrap>(server, queuedSettings, &session, runner);
		auto notifications = std::vector<bool>();
		auto lifetime = rpl::lifetime();
		queued->waitingForTakeout(
		) | rpl::on_next([&](bool waiting) {
			notifications.push_back(waiting);
		}, lifetime);
		auto started = false;
		queued->startMainSession([&] { started = true; });
		Require(callbacks.size() == 1, "self-id response was not queued");
		auto self = std::move(callbacks.front());
		callbacks.pop_front();
		self();
		Require(callbacks.size() == 1, "waiting callback was not queued through actor runner");
		Require(notifications.empty(), "waiting notification bypassed actor runner");
		if (destroy) {
			queued.reset();
		} else {
			queued->cancelExportFast();
		}
		while (!callbacks.empty()) {
			auto next = std::move(callbacks.front());
			callbacks.pop_front();
			next();
		}
		Require(notifications.empty() && !started, "stopped actor received a late waiting notification");
		first.finishExport([] {});
		Require(server.initializations.size() == 1, "stopped waiting actor initialized after cancellation");
	}
}

void ControllerWaitingOnlyChangesInitializingState() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto api = Export::ApiWrap(server, Export::Settings(), &session);
	auto state = Export::ControllerObject(api, u"/normalized/export_3/"_q);
	auto changes = 0;
	auto lifetime = rpl::lifetime();
	state.states() | rpl::on_next([&](const Export::State&) {
		++changes;
	}, lifetime);
	state.waitingForTakeoutChanged(true);
	Require(changes == 1, "waiting notification changed settings state");
	auto processing = Export::ProcessingState();
	processing.outputPath = u"/unreserved/requested/path/"_q;
	state.setState(std::move(processing));
	state.waitingForTakeoutChanged(true);
	state.waitingForTakeoutChanged(true);
	Require(changes == 3, "waiting notification repeated an unchanged state");
	state.waitingForTakeoutChanged(false);
	Require(changes == 4, "initializing notification did not clear waiting");
	processing = Export::ProcessingState();
	processing.step = Export::ProcessingState::Step::Dialogs;
	state.setState(std::move(processing));
	state.waitingForTakeoutChanged(true);
	Require(changes == 5, "waiting notification changed running dialog state");
	Require(std::get<Export::ProcessingState>(state.currentState()).outputPath
		== u"/normalized/export_3/"_q,
		"ordinary processing state did not retain normalized output path");
	state.setState(Export::CancelledState());
	state.waitingForTakeoutChanged(true);
	state.setState(Export::ProcessingState());
	Require(changes == 6, "waiting notification revived cancelled controller");
}

void CancellationDropsOnlyOwnOutstandingRequest() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto first = Export::ApiWrap(server, Export::Settings(), &session);
	auto second = Export::ApiWrap(server, Export::Settings(), &session);
	first.startMainSession([] {});
	second.startMainSession([] {});
	server.defer = true;
	auto firstReceived = false;
	auto secondReceived = false;
	first.requestProbe([&] { firstReceived = true; });
	second.requestProbe([&] { secondReceived = true; });
	first.cancelExportFast();
	server.drain();
	Require(!firstReceived, "cancelled chat received outstanding response");
	Require(secondReceived, "cancelling first chat dropped second chat response");
	Require(server.finishes.empty(), "cancelling first chat terminated takeout");
	server.defer = false;
	Require(second.probe(), "surviving chat takeout became invalid");
}

void CompletionFailureAfterActorDestructionIsSuppressed() {
	auto server = MTP::Server();
	auto session = Export::TakeoutSession(&server);
	auto callbacks = std::deque<FnMut<void()>>();
	auto runner = [&](FnMut<void()> callback) { callbacks.push_back(std::move(callback)); };
	auto chat = std::make_unique<Export::ApiWrap>(server, Export::Settings(), &session, runner);
	auto started = false;
	auto completed = false;
	chat->startMainSession([&] { started = true; });
	while (!callbacks.empty()) {
		auto next = std::move(callbacks.front());
		callbacks.pop_front();
		next();
	}
	Require(started, "chat did not start before actor destruction");
	server.defer = true;
	server.failNextFinish = MTP::Error{ "FINISH_FAILED" };
	chat->finishExport([&] { completed = true; });
	server.drain();
	Require(callbacks.size() == 1, "finish failure callback was not queued");
	chat.reset();
	while (!callbacks.empty()) {
		auto next = std::move(callbacks.front());
		callbacks.pop_front();
		next();
	}
	Require(!completed, "destroyed actor received completion callback");
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
		{ "single_chat_completes_successfully", SingleChatCompletesSuccessfully },
		{ "single_chat_cancels_without_success", SingleChatCancelsWithoutSuccess },
		{ "takeout_permission_flags_match_settings", TakeoutPermissionFlagsMatchSettings },
		{ "starting_second_chat_keeps_first_chat_alive", StartingSecondChatKeepsFirstChatAlive },
		{ "cancel_before_self_response", CancellationBeforeSelfResponseCannotInitialize },
		{ "cancel_before_main_acquire", CancellationBeforeMainAcquireCannotInitialize },
		{ "cancel_before_ready_callback", CancellationBeforeReadyCannotReviveChat },
		{ "queued_api_waiting_and_initializing", QueuedApiReportsWaitingAndInitializingBeforeReady },
		{ "compatible_api_never_waits", CompatibleApiNeverReportsWaiting },
		{ "cancelled_actor_drops_waiting_notifications", CancelledActorDropsPendingWaitingNotifications },
		{ "controller_waiting_only_changes_initializing", ControllerWaitingOnlyChangesInitializingState },
		{ "cancel_drops_only_own_request", CancellationDropsOnlyOwnOutstandingRequest },
		{ "finish_failure_after_actor_destruction", CompletionFailureAfterActorDestructionIsSuppressed },
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
	std::cout << passed << '/' << total << " takeout regression tests passed\n";
	return passed == total && total != 0 ? 0 : 1;
}
