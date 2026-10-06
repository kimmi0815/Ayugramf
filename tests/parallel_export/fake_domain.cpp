#include "fake_domain.h"
#include "export/export_takeout_session.h"

rpl::producer<Main::Session*> Main::Account::sessionChanges() const {
	return _changes.events();
}

void Main::Account::changeSession(Session *session) {
	_changes.fire_copy(session);
}

void Data::Session::add(PeerData *peer) {
	_peers.push_back(peer);
}

PeerData *Data::Session::find(uint64 peerId) const {
	const auto i = ranges::find_if(_peers, [=](const auto peer) {
		return peer->id.value == peerId;
	});
	return i == _peers.end() ? nullptr : *i;
}

PeerData *Data::PeerFromInputMTP(
		not_null<Session*> session,
		const MTPInputPeer &input) {
	return session->find(input.peerId);
}

Main::Session::Session(Account *account)
: _account(account ? account : &_ownedAccount)
, _transport{ this } {
}

Main::Account &Main::Session::account() {
	return *_account;
}

Data::Session &Main::Session::data() {
	return _data;
}

Main::Transport &Main::Session::mtp() {
	return _transport;
}

rpl::lifetime &Main::Session::lifetime() {
	return _lifetime;
}

PeerData::PeerData(not_null<Main::Session*> session, uint64 peerId)
: id{ peerId }
, _session(session) {
	session->data().add(this);
}

Main::Session &PeerData::session() const {
	return *_session;
}

MTPInputPeer PeerData::input() const {
	return { id.value };
}

Export::Controller::Controller(
		Main::Transport *mtp,
		base::weak_qptr<TakeoutSession> takeout,
		const MTPInputPeer &peer)
: _session(mtp->session)
, _peerId(peer.peerId) {
	if (!takeout || takeout.get()->owner != _session) {
		throw std::runtime_error("Controller received another account's takeout owner");
	}
	_live.push_back(this);
}

Export::Controller::Controller(
		Main::Transport *mtp,
		base::weak_qptr<TakeoutSession> takeout,
		const MTPInputPeer &peer,
		int32 topicRootId,
		uint64 peerId,
		const QString &topicTitle)
: Controller(mtp, takeout, peer) {
	_topicRootId = topicRootId;
}

Export::Controller::~Controller() {
	_live.erase(std::find(_live.begin(), _live.end(), this));
}

rpl::producer<Export::State> Export::Controller::state() const {
	return _source->changes.events_starting_with_copy(_source->current);
}

rpl::lifetime &Export::Controller::lifetime() {
	return _lifetime;
}

void Export::Controller::setState(State state) {
	const auto source = _source;
	source->current = std::move(state);
	source->changes.fire_copy(source->current);
}

std::shared_ptr<Export::Controller::Source> Export::Controller::source() const {
	return _source;
}

Export::Controller *Export::Controller::Find(
		Main::Session *session,
		uint64 peerId,
		int32 topicRootId) {
	const auto i = ranges::find_if(_live, [=](const auto controller) {
		return controller->_session == session
			&& controller->_peerId == peerId
			&& controller->_topicRootId == topicRootId;
	});
	return i == _live.end() ? nullptr : *i;
}

std::size_t Export::Controller::Count() {
	return _live.size();
}

Export::View::PanelController::PanelController(
		not_null<Main::Session*> session,
		not_null<Controller*> controller)
: _session(session)
, _controller(controller) {
	_live.push_back(this);
	controller->state() | rpl::on_next([=](const State &state) {
		_processing = v::is<ProcessingState>(state);
	}, _lifetime);
}

Export::View::PanelController::~PanelController() {
	_live.erase(std::find(_live.begin(), _live.end(), this));
}

void Export::View::PanelController::activatePanel() {
	++_activations;
}

Main::Session &Export::View::PanelController::session() const {
	return *_session;
}

void Export::View::PanelController::stopWithConfirmation(Fn<void()> callback) {
	if (_processing) {
		_confirmation = std::move(callback);
	} else {
		callback();
	}
}

void Export::View::PanelController::confirmStop() {
	base::take(_confirmation)();
}

void Export::View::PanelController::requestStop() {
	const auto stops = _stops;
	stops->fire({});
}

rpl::producer<> Export::View::PanelController::stopRequests() const {
	return _stops->events();
}

rpl::lifetime &Export::View::PanelController::lifetime() {
	return _lifetime;
}

int Export::View::PanelController::activations() const {
	return _activations;
}

bool Export::View::PanelController::awaitingConfirmation() const {
	return bool(_confirmation);
}

Export::View::PanelController *Export::View::PanelController::Find(
		Controller *controller) {
	const auto i = ranges::find_if(_live, [=](const auto panel) {
		return panel->_controller == controller;
	});
	return i == _live.end() ? nullptr : *i;
}

std::size_t Export::View::PanelController::Count() {
	return _live.size();
}
