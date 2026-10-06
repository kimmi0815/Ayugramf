/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_manager.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "export/view/export_view_panel_controller.h"
#include "export/export_controller.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/layers/box_content.h"

namespace Export {

struct Manager::Job {
	uint64 id = 0;
	Main::Session *session = nullptr;
	uint64 peerId = 0;
	int32 topicRootId = 0;
	bool processing = false;
	bool finished = false;
	std::unique_ptr<Controller> controller;
	std::unique_ptr<View::PanelController> panel;
};

Manager::Manager() = default;

Manager::~Manager() = default;

void Manager::start(not_null<PeerData*> peer) {
	start(&peer->session(), peer->input());
}

void Manager::startTopic(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QString &topicTitle) {
	const auto session = &peer->session();
	const auto peerId = uint64(peer->id.value);
	const auto rootId = int32(topicRootId.bare);
	if (const auto job = find(session, peerId, rootId)) {
		job->panel->activatePanel();
		return;
	}
	auto job = std::make_unique<Job>();
	job->id = ++_nextId;
	job->session = session;
	job->peerId = peerId;
	job->topicRootId = rootId;
	job->controller = std::make_unique<Controller>(
		&session->mtp(),
		peer->input(),
		rootId,
		peerId,
		topicTitle);
	setupPanel(std::move(job));
}

void Manager::start(
		not_null<Main::Session*> session,
		const MTPInputPeer &singlePeer) {
	const auto peer = Data::PeerFromInputMTP(&session->data(), singlePeer);
	const auto peerId = peer ? uint64(peer->id.value) : uint64(0);
	if (const auto job = find(session, peerId, 0)) {
		job->panel->activatePanel();
		return;
	}
	auto job = std::make_unique<Job>();
	job->id = ++_nextId;
	job->session = session;
	job->peerId = peerId;
	job->controller = std::make_unique<Controller>(
		&session->mtp(),
		singlePeer);
	setupPanel(std::move(job));
}

Manager::Job *Manager::find(
		not_null<Main::Session*> session,
		uint64 peerId,
		int32 topicRootId) const {
	for (const auto &job : _jobs) {
		if (job->session == session
			&& job->peerId == peerId
			&& job->topicRootId == topicRootId) {
			return job.get();
		}
	}
	return nullptr;
}

void Manager::setupPanel(std::unique_ptr<Job> job) {
	const auto id = job->id;
	const auto session = not_null(job->session);
	const auto entry = job.get();
	job->panel = std::make_unique<View::PanelController>(
		session,
		job->controller.get());
	session->account().sessionChanges(
	) | rpl::filter([=](Main::Session *value) {
		return (value != session);
	}) | rpl::on_next([=] {
		stop(id);
	}, job->panel->lifetime());

	job->panel->stopRequests(
	) | rpl::on_next([=] {
		LOG(("Export Info: Stop requested for job %1.").arg(id));
		stop(id);
	}, job->controller->lifetime());
	job->controller->state(
	) | rpl::on_next([=](const State &state) {
		const auto processing = v::is<ProcessingState>(state);
		const auto finished = v::is<FinishedState>(state)
			|| v::is<CancelledState>(state)
			|| v::is<ApiErrorState>(state)
			|| v::is<OutputErrorState>(state);
		if (entry->processing != processing || entry->finished != finished) {
			entry->processing = processing;
			entry->finished = finished;
			_viewChanges.fire({});
		}
	}, job->controller->lifetime());
	_jobs.push_back(std::move(job));
	_viewChanges.fire({});
}

View::PanelController *Manager::currentPanel(
		Main::Session *session,
		bool processingOnly) const {
	for (auto i = _jobs.rbegin(); i != _jobs.rend(); ++i) {
		if ((*i)->processing && (!session || (*i)->session == session)) {
			return (*i)->panel.get();
		}
	}
	if (processingOnly) {
		return nullptr;
	}
	for (auto i = _jobs.rbegin(); i != _jobs.rend(); ++i) {
		if (!(*i)->finished && (!session || (*i)->session == session)) {
			return (*i)->panel.get();
		}
	}
	for (auto i = _jobs.rbegin(); i != _jobs.rend(); ++i) {
		if (!session || (*i)->session == session) {
			return (*i)->panel.get();
		}
	}
	return nullptr;
}

rpl::producer<View::PanelController*> Manager::currentView(
		Main::Session *session) const {
	return _viewChanges.events_starting_with({}) | rpl::map([=] {
		return currentPanel(session, true);
	}) | rpl::distinct_until_changed();
}

bool Manager::inProgress() const {
	return !_jobs.empty();
}

bool Manager::inProgress(not_null<Main::Session*> session) const {
	return currentPanel(session) != nullptr;
}

void Manager::stopWithConfirmation(
		Fn<void()> callback,
		Main::Session *session) {
	const auto panel = currentPanel(session);
	if (!panel) {
		callback();
		return;
	}
	const auto i = ranges::find_if(_jobs, [=](const auto &job) {
		return job->panel.get() == panel;
	});
	Assert(i != end(_jobs));
	const auto id = (*i)->id;
	panel->stopWithConfirmation([=, callback = std::move(callback)]() mutable {
		auto saved = std::move(callback);
		LOG(("Export Info: Stop With Confirmation."));
		stop(id);
		stopWithConfirmation(std::move(saved), session);
	});
}

void Manager::stop(uint64 id) {
	const auto i = ranges::find_if(_jobs, [=](const auto &job) {
		return job->id == id;
	});
	if (i == end(_jobs)) {
		return;
	}
	LOG(("Export Info: Destroying job %1.").arg(id));
	auto stopped = std::move(*i);
	_jobs.erase(i);
	_viewChanges.fire({});
}

void Manager::stop() {
	auto stopped = base::take(_jobs);
	_viewChanges.fire({});
}

} // namespace Export
