/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"

class PeerData;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Export {

class Controller;

struct JobInfo {
	uint64 id = 0;
	QString title;
	State state;
};

namespace View {
class PanelController;
} // namespace View

class Manager final {
public:
	Manager();
	~Manager();

	void start(not_null<PeerData*> peer);
	void start(
		not_null<Main::Session*> session,
		const MTPInputPeer &singlePeer = MTP_inputPeerEmpty());
	void startTopic(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QString &topicTitle);

	[[nodiscard]] rpl::producer<View::PanelController*> currentView(
		Main::Session *session = nullptr) const;
	[[nodiscard]] rpl::producer<std::vector<JobInfo>> jobs(
		not_null<Main::Session*> session) const;
	void activate(uint64 id, not_null<Main::Session*> session);
	[[nodiscard]] bool inProgress() const;
	[[nodiscard]] bool inProgress(not_null<Main::Session*> session) const;
	void stopWithConfirmation(
		Fn<void()> callback,
		Main::Session *session = nullptr);
	void stop();

private:
	struct Job;
	[[nodiscard]] Job *find(
		not_null<Main::Session*> session,
		uint64 peerId,
		int32 topicRootId) const;
	[[nodiscard]] View::PanelController *currentPanel(
		Main::Session *session,
		bool processingOnly = false) const;
	void setupPanel(std::unique_ptr<Job> job);
	void stop(uint64 id);

	std::vector<std::unique_ptr<Job>> _jobs;
	uint64 _nextId = 0;
	rpl::event_stream<> _viewChanges;
	rpl::event_stream<> _jobChanges;

};

} // namespace Export
