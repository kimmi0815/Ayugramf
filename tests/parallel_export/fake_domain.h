#pragma once

class PeerData;

namespace Main {
class Session;

struct Transport {
	Session *session = nullptr;
};

class Account {
public:
	rpl::producer<Session*> sessionChanges() const;
	void changeSession(Session *session);

private:
	rpl::event_stream<Session*> _changes;

};

}

namespace Data {

class Session {
public:
	void add(PeerData *peer);
	PeerData *find(uint64 peerId) const;

private:
	std::vector<PeerData*> _peers;

};

PeerData *PeerFromInputMTP(
	not_null<Session*> session,
	const MTPInputPeer &input);

}

namespace Main {

class Session {
public:
	explicit Session(Account *account = nullptr);
	Account &account();
	Data::Session &data();
	Transport &mtp();
	rpl::lifetime &lifetime();

private:
	Account _ownedAccount;
	Account *_account = nullptr;
	Data::Session _data;
	Transport _transport;
	rpl::lifetime _lifetime;

};

}

class PeerData {
public:
	PeerData(not_null<Main::Session*> session, uint64 peerId);
	Main::Session &session() const;
	MTPInputPeer input() const;

	struct Id {
		uint64 value = 0;
	};
	Id id;

private:
	const not_null<Main::Session*> _session;

};

namespace Export {

class TakeoutSession;

struct PasswordCheckState {
};
struct ProcessingState {
};
struct FinishedState {
};
struct CancelledState {
};
struct ApiErrorState {
};
struct OutputErrorState {
};

using State = std::variant<
	PasswordCheckState,
	ProcessingState,
	FinishedState,
	CancelledState,
	ApiErrorState,
	OutputErrorState>;

class Controller {
public:
	struct Source {
		State current = PasswordCheckState();
		rpl::event_stream<State> changes;
	};

	Controller(
		Main::Transport *mtp,
		base::weak_qptr<TakeoutSession> takeout,
		const MTPInputPeer &peer);
	Controller(
		Main::Transport *mtp,
		base::weak_qptr<TakeoutSession> takeout,
		const MTPInputPeer &peer,
		int32 topicRootId,
		uint64 peerId,
		const QString &topicTitle);
	~Controller();

	rpl::producer<State> state() const;
	rpl::lifetime &lifetime();
	void setState(State state);
	std::shared_ptr<Source> source() const;
	static Controller *Find(
		Main::Session *session,
		uint64 peerId,
		int32 topicRootId = 0);
	static std::size_t Count();

private:
	inline static std::vector<Controller*> _live;
	Main::Session *_session = nullptr;
	uint64 _peerId = 0;
	int32 _topicRootId = 0;
	std::shared_ptr<Source> _source = std::make_shared<Source>();
	rpl::lifetime _lifetime;

};

namespace View {

class PanelController {
public:
	PanelController(not_null<Main::Session*> session, not_null<Controller*> controller);
	~PanelController();

	void activatePanel();
	Main::Session &session() const;
	void stopWithConfirmation(Fn<void()> callback);
	void confirmStop();
	void requestStop();
	rpl::producer<> stopRequests() const;
	rpl::lifetime &lifetime();
	int activations() const;
	bool awaitingConfirmation() const;
	static PanelController *Find(Controller *controller);
	static std::size_t Count();

private:
	inline static std::vector<PanelController*> _live;
	const not_null<Main::Session*> _session;
	const not_null<Controller*> _controller;
	std::shared_ptr<rpl::event_stream<>> _stops = std::make_shared<rpl::event_stream<>>();
	Fn<void()> _confirmation;
	bool _processing = false;
	int _activations = 0;
	rpl::lifetime _lifetime;

};

}

}
