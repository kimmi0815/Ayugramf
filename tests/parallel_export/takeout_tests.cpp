#include "takeout_domain.h"
#include "takeout_api.inc"

namespace Export {

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
	Require(callbacks.size() == 1, "takeout ready response was not queued");
	chat.cancelExportFast();
	while (!callbacks.empty()) {
		auto next = std::move(callbacks.front());
		callbacks.pop_front();
		next();
	}
	Require(!started && !chat.takeoutId(), "late ready revived cancelled chat");
	Require(!server.active, "cancelled ready takeout was not cleaned up");
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
