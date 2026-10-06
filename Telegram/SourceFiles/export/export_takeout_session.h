/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_concurrent_sender.h"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Export {

class TakeoutSession final : public QObject {
public:
	struct Lease {
		MTPaccount_InitTakeoutSession::Flags flags;
		int64 sizeLimit = 0;
		Fn<void(FnMut<void()>)> runner;
		Fn<void(bool)> waiting;
		FnMut<void(uint64)> ready;
		Fn<void(const MTP::Error&)> failed;
		std::atomic<bool> cancelled = false;
	};

	explicit TakeoutSession(base::weak_qptr<MTP::Instance> weak);
	~TakeoutSession() override;

	[[nodiscard]] static base::weak_qptr<TakeoutSession> ForSession(
		not_null<Main::Session*> session);

	void acquire(std::shared_ptr<Lease> lease);
	void release(
		std::shared_ptr<Lease> lease,
		bool success,
		FnMut<void()> done = nullptr);

private:
	struct Initialization;

	[[nodiscard]] bool compatible(const Lease &lease) const;
	void initialize();
	void initialized(uint64 id);
	void initializationFailed(const MTP::Error &error);
	void notifyWaiting(const std::shared_ptr<Lease> &lease, bool waiting);
	void notifyReady(const std::shared_ptr<Lease> &lease);
	void finish(std::shared_ptr<Lease> lease, FnMut<void()> done);
	void finished();
	void startQueued();

	base::weak_qptr<MTP::Instance> _instance;
	MTP::ConcurrentSender _mtp;
	std::shared_ptr<Initialization> _initialization;
	std::optional<uint64> _takeoutId;
	MTPaccount_InitTakeoutSession::Flags _flags;
	int64 _sizeLimit = 0;
	mtpRequestId _requestId = 0;
	std::vector<std::shared_ptr<Lease>> _leases;
	std::vector<std::shared_ptr<Lease>> _queued;
	bool _initializing = false;
	bool _finishing = false;
	bool _success = true;

};

} // namespace Export
