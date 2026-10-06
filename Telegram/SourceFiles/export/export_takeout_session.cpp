/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/export_takeout_session.h"

#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_response.h"

namespace Export {

struct TakeoutSession::Initialization {
	Initialization(
		base::weak_qptr<MTP::Instance> instance,
		base::weak_qptr<TakeoutSession> session);
	void completed(uint64 id);
	void failed(const MTP::Error &error);

	MTP::ConcurrentSender sender;
	QObject lifetime;
	base::weak_qptr<TakeoutSession> session;
	mtpRequestId requestId = 0;
};

TakeoutSession::Initialization::Initialization(
	base::weak_qptr<MTP::Instance> instance,
	base::weak_qptr<TakeoutSession> session)
: sender(instance, [](FnMut<void()> callback) {
	crl::on_main(std::move(callback));
})
, session(session) {
}

void TakeoutSession::Initialization::completed(uint64 id) {
	if (const auto strong = session.get()) {
		strong->initialized(id);
	} else {
		const auto requestId = sender.request(MTPInvokeWithTakeout<
			MTPaccount_FinishTakeoutSession>(
				MTP_long(id),
				MTPaccount_FinishTakeoutSession(MTP_flags(0))
		)).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
		sender.request(requestId).detach();
	}
}

void TakeoutSession::Initialization::failed(const MTP::Error &error) {
	if (const auto strong = session.get()) {
		strong->initializationFailed(error);
	}
}

TakeoutSession::TakeoutSession(base::weak_qptr<MTP::Instance> weak)
: _instance(weak)
, _mtp(weak, [](FnMut<void()> callback) {
	crl::on_main(std::move(callback));
}) {
}

bool TakeoutSession::compatible(const Lease &lease) const {
	using Flag = MTPaccount_InitTakeoutSession::Flag;
	return ((_flags & lease.flags) == lease.flags)
		&& (!(lease.flags & Flag::f_files)
			|| lease.sizeLimit <= _sizeLimit);
}

void TakeoutSession::acquire(std::shared_ptr<Lease> lease) {
	if (lease->cancelled) {
		return;
	}
	if (_finishing
		|| ((_initializing || _takeoutId) && !compatible(*lease))) {
		_queued.push_back(std::move(lease));
		return;
	}
	_leases.push_back(std::move(lease));
	if (_takeoutId) {
		notifyReady(_leases.back());
	} else if (!_initializing) {
		_flags = _leases.front()->flags;
		_sizeLimit = _leases.front()->sizeLimit;
		_success = true;
		initialize();
	}
}

void TakeoutSession::initialize() {
	_initializing = true;
	if (!_instance) {
		initializationFailed(MTP::Error::Local(
			u"EXPORT_SESSION_CLOSED"_q,
			u"The account transport is no longer available."_q));
		return;
	}
	_initialization = std::make_shared<Initialization>(
		_instance,
		base::weak_qptr<TakeoutSession>(this));
	const auto state = _initialization;
	QObject::connect(
		_instance.get(),
		&QObject::destroyed,
		&state->lifetime,
		[state = std::weak_ptr<Initialization>(state)] {
			if (const auto strong = state.lock()) {
				strong->sender.request(strong->requestId).cancel();
			}
		});
	state->requestId = state->sender.request(MTPaccount_InitTakeoutSession(
		MTP_flags(_flags),
		MTP_long(_sizeLimit)
	)).done([=](const MTPaccount_Takeout &result) {
		state->completed(result.match([](const MTPDaccount_takeout &data) {
			return data.vid().v;
		}));
	}).fail([=](const MTP::Error &error) {
		state->failed(error);
	}).handleAllErrors(
	).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
}

void TakeoutSession::initialized(uint64 id) {
	_initialization = nullptr;
	_initializing = false;
	_takeoutId = id;
	const auto leases = _leases;
	for (const auto &lease : leases) {
		if (lease->cancelled) {
			_success = false;
			const auto i = ranges::find(_leases, lease);
			if (i != end(_leases)) {
				_leases.erase(i);
			}
		} else if (_takeoutId) {
			notifyReady(lease);
		}
	}
	if (_leases.empty() && _takeoutId && !_finishing) {
		finish(nullptr, nullptr);
	}
}

void TakeoutSession::initializationFailed(const MTP::Error &error) {
	_initialization = nullptr;
	_initializing = false;
	for (const auto &lease : base::take(_leases)) {
		lease->runner([=] {
			if (!lease->cancelled) {
				lease->failed(error);
			}
		});
	}
	startQueued();
}

void TakeoutSession::notifyReady(const std::shared_ptr<Lease> &lease) {
	const auto id = *_takeoutId;
	lease->runner([=] {
		if (!lease->cancelled) {
			base::take(lease->ready)(id);
		}
	});
}

void TakeoutSession::release(
		std::shared_ptr<Lease> lease,
		bool success,
		FnMut<void()> done) {
	lease->cancelled = true;
	const auto i = ranges::find(_leases, lease);
	if (i != end(_leases)) {
		_success = _success && success;
		_leases.erase(i);
		if (_leases.empty() && _takeoutId) {
			finish(std::move(lease), std::move(done));
			return;
		}
	} else {
		const auto queued = ranges::find(_queued, lease);
		if (queued != end(_queued)) {
			_queued.erase(queued);
		}
	}
	if (done) {
		lease->runner(std::move(done));
	}
}

void TakeoutSession::finish(
		std::shared_ptr<Lease> lease,
		FnMut<void()> done) {
	Expects(_takeoutId.has_value());
	_finishing = true;
	using Flag = MTPaccount_FinishTakeoutSession::Flag;
	_requestId = _mtp.request(MTPInvokeWithTakeout<
		MTPaccount_FinishTakeoutSession>(
			MTP_long(*_takeoutId),
			MTPaccount_FinishTakeoutSession(MTP_flags(
				_success ? Flag::f_success : Flag(0)))
	)).done([=, done = std::move(done)]() mutable {
		finished();
		if (done) {
			lease->runner(std::move(done));
		}
	}).fail([=](const MTP::Error &error) {
		finished();
		if (lease) {
			lease->runner([=] { lease->failed(error); });
		}
	}).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
}

void TakeoutSession::finished() {
	_requestId = 0;
	_finishing = false;
	_takeoutId = std::nullopt;
	startQueued();
}

void TakeoutSession::startQueued() {
	for (auto &lease : base::take(_queued)) {
		acquire(std::move(lease));
	}
}

TakeoutSession::~TakeoutSession() {
	for (const auto &lease : _leases) {
		lease->cancelled = true;
	}
	for (const auto &lease : _queued) {
		lease->cancelled = true;
	}
	if (_takeoutId && !_finishing) {
		const auto requestId = _mtp.request(MTPInvokeWithTakeout<
			MTPaccount_FinishTakeoutSession>(
				MTP_long(*_takeoutId),
				MTPaccount_FinishTakeoutSession(MTP_flags(0))
		)).toDC(MTP::ShiftDcId(0, MTP::kExportDcShift)).send();
		_mtp.request(requestId).detach();
	} else if (_finishing && _requestId) {
		_mtp.request(_requestId).detach();
	}
}

} // namespace Export
