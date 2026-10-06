#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(_LIBCPP_VERSION) && !defined(_LIBCPP_TEMPLATE_VIS)
#define _LIBCPP_TEMPLATE_VIS
#endif

#include <gsl/gsl>
#include "base/flags.h"
#include "rpl/rpl.h"

using int32 = std::int32_t;
using int64 = std::int64_t;
using uint64 = std::uint64_t;
using mtpRequestId = int;
using UserId = uint64;
using TimeId = int64;
using QString = std::string;
using QByteArray = std::string;
using gsl::not_null;

template <typename Signature>
using Fn = std::function<Signature>;

template <typename Signature>
using FnMut = std::function<Signature>;

namespace ranges {
using std::ranges::find;
}

namespace crl {

inline bool deferMain = false;
inline std::deque<FnMut<void()>> pendingMain;

inline void on_main(FnMut<void()> callback) {
	if (deferMain) {
		pendingMain.push_back(std::move(callback));
	} else {
		callback();
	}
}

template <typename Context, typename Callback>
void on_main(Context context, Callback callback) {
	on_main([context, callback = std::move(callback)]() mutable {
		if (context.get()) {
			callback();
		}
	});
}

inline void drain() {
	while (!pendingMain.empty()) {
		auto next = std::move(pendingMain.front());
		pendingMain.pop_front();
		next();
	}
}

}

class QObject {
public:
	explicit QObject(QObject * = nullptr) {}
	virtual ~QObject() { emitDestroyed(); }
	std::shared_ptr<int> testLifetime = std::make_shared<int>(0);
	void destroyed() {}
	void emitDestroyed() {
		if (!testLifetime) {
			return;
		}
		testLifetime.reset();
		for (const auto &callback : std::exchange(_destroyedCallbacks, {})) {
			callback();
		}
	}
	template <typename Sender, typename Signal, typename Context, typename Handler>
	static void connect(Sender *sender, Signal, Context *context, Handler handler) {
		if (sender) {
			sender->_destroyedCallbacks.push_back([
				weak = std::weak_ptr<int>(context->testLifetime),
				handler = std::move(handler)
			] {
				if (!weak.expired()) {
					handler();
				}
			});
		}
	}

private:
	std::vector<Fn<void()>> _destroyedCallbacks;

};

inline QString operator"" _q(const char16_t *text, std::size_t size) {
	return QString(text, text + size);
}

namespace base {

namespace assertion {

inline void log(const char *message, const char *file, int line) {
	std::cerr << file << ':' << line << ": " << message << '\n';
}

}

template <typename Type>
class weak_qptr {
public:
	weak_qptr() = default;
	weak_qptr(Type *value)
	: _value(value), _lifetime(value ? value->testLifetime : nullptr) {
	}
	Type *get() const { return _lifetime.expired() ? nullptr : _value; }
	Type *operator->() const { return get(); }
	explicit operator bool() const { return get() != nullptr; }

private:
	Type *_value = nullptr;
	std::weak_ptr<int> _lifetime;

};

template <typename Type>
weak_qptr<Type> make_weak(Type *value) {
	return value;
}

template <typename Type>
Type take(Type &value) {
	return std::exchange(value, Type{});
}

}

struct MTPlong {
	uint64 v = 0;
};

inline MTPlong MTP_long(uint64 value) {
	return { value };
}

struct MTPInputPeer {
	int type() const { return 0; }
};

inline constexpr auto mtpc_inputPeerEmpty = 0;

inline MTPInputPeer MTP_inputPeerEmpty() {
	return {};
}

struct MTPInputUser {
};

inline MTPInputUser MTP_inputUserSelf() {
	return {};
}

template <typename Type>
struct MTPVector {
	std::vector<Type> v;
};

template <typename Type>
MTPVector<Type> MTP_vector(int count, Type value) {
	return { std::vector<Type>(count, value) };
}

struct MTPDuser {
	bool is_self() const { return true; }
	uint64 vid() const { return 7; }
};

struct MTPDuserEmpty {
};

struct MTPUser {
	template <typename Present, typename Empty>
	void match(Present &&present, Empty &&) const {
		present(MTPDuser());
	}
};

struct MTPDaccount_takeout {
	MTPlong id;
	MTPlong vid() const { return id; }
};

struct MTPaccount_Takeout {
	MTPDaccount_takeout data;
	template <typename Callback>
	auto match(Callback &&callback) const {
		return callback(data);
	}
};

struct MTPusers_GetUsers {
	using ResponseType = MTPVector<MTPUser>;
	MTPVector<MTPInputUser> users;
};

struct MTPaccount_InitTakeoutSession {
	using ResponseType = MTPaccount_Takeout;
	enum class Flag {
		f_contacts = 1,
		f_message_users = 2,
		f_message_chats = 4,
		f_message_megagroups = 8,
		f_message_channels = 16,
		f_files = 32,
		f_file_max_size = 32,
	};
	friend constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;
	int flags = 0;
	MTPlong maxSize;
};

struct MTPaccount_FinishTakeoutSession {
	using ResponseType = bool;
	enum class Flag {
		f_success = 1,
	};
	friend constexpr bool is_flag_type(Flag) { return true; }
	int flags = 0;
};

template <typename Flags>
int MTP_flags(Flags flags) {
	return static_cast<int>(flags);
}

template <typename Request>
struct MTPInvokeWithTakeout {
	using ResponseType = typename Request::ResponseType;
	MTPlong id;
	Request request;
};

#include "export/export_settings.h"

namespace MTP {

struct Error {
	std::string name;
	int status = 400;
	QString type() const { return name; }
	int code() const { return status; }
	static Error Local(const QString &name, const QString &) { return { name }; }
};

inline constexpr auto kExportDcShift = 1;

inline int ShiftDcId(int id, int shift) {
	return id + shift;
}

class Server : public QObject {
public:
	~Server() override { emitDestroyed(); }
	struct Init {
		int flags = 0;
		uint64 maxSize = 0;
	};
	std::optional<uint64> active;
	std::vector<Init> initializations;
	std::vector<std::pair<uint64, int>> finishes;
	uint64 nextId = 1;
	bool defer = false;
	bool missingSelf = false;
	std::optional<Error> failNextInit;
	std::optional<Error> failNextFinish;
	int suppressedDefaultErrors = 0;
	std::deque<FnMut<void()>> pending;

	bool drainOne() {
		if (pending.empty()) {
			return false;
		}
		auto next = std::move(pending.front());
		pending.pop_front();
		next();
		return true;
	}

	void drain() {
		for (auto iterations = 0; drainOne(); ++iterations) {
			if (iterations > 1000) {
				throw std::runtime_error("transport did not become idle");
			}
		}
	}

	MTPVector<MTPUser> execute(const MTPusers_GetUsers &) {
		return missingSelf ? MTPVector<MTPUser>() : MTPVector<MTPUser>{ { MTPUser() } };
	}

	MTPaccount_Takeout execute(const MTPaccount_InitTakeoutSession &request) {
		initializations.push_back({ request.flags, request.maxSize.v });
		if (failNextInit) {
			throw *base::take(failNextInit);
		}
		active = nextId++;
		return { { MTP_long(*active) } };
	}

	template <typename Request>
	typename Request::ResponseType execute(
			const MTPInvokeWithTakeout<Request> &request) {
		if (active != request.id.v) {
			throw Error{ "TAKEOUT_INVALID" };
		}
		return executeWrapped(request.id.v, request.request);
	}

private:
	MTPVector<MTPUser> executeWrapped(uint64, const MTPusers_GetUsers &request) {
		return execute(request);
	}

	bool executeWrapped(uint64 id, const MTPaccount_FinishTakeoutSession &request) {
		finishes.emplace_back(id, request.flags);
		if (failNextFinish) {
			throw *base::take(failNextFinish);
		}
		active.reset();
		return true;
	}

};

using Instance = Server;

class ConcurrentSender {
public:
	explicit ConcurrentSender(Server &server) : _server(server) {
	}
	ConcurrentSender(base::weak_qptr<Instance> server, Fn<void(FnMut<void()>)> runner)
	: _server(*server.get()), _runner(std::move(runner)) {
	}
	~ConcurrentSender() {
		for (const auto &entry : _requests) {
			if (!entry.second->detached) {
				entry.second->cancelled = true;
			}
		}
	}

	struct RequestState {
		bool cancelled = false;
		bool detached = false;
	};

	template <typename Request>
	class SpecificRequestBuilder {
	public:
		using Response = typename Request::ResponseType;
		SpecificRequestBuilder(ConcurrentSender &sender, Request request)
		: _sender(sender), _request(std::move(request)) {
		}

		template <typename Handler>
		SpecificRequestBuilder &done(Handler handler) {
			_done = [handler = std::move(handler)](Response result) mutable {
				if constexpr (std::is_invocable_v<Handler, Response>) {
					handler(std::move(result));
				} else {
					handler();
				}
			};
			return *this;
		}

		template <typename Handler>
		SpecificRequestBuilder &fail(Handler handler) {
			_fail = std::move(handler);
			return *this;
		}

		SpecificRequestBuilder &toDC(int) { return *this; }
		SpecificRequestBuilder &handleAllErrors() {
			_handleAllErrors = true;
			return *this;
		}
		SpecificRequestBuilder &handleFloodErrors() { return *this; }

		mtpRequestId send() {
			const auto id = _sender._nextRequestId++;
			const auto state = std::make_shared<RequestState>();
			_sender._requests.emplace(id, state);
			auto perform = [
				state,
				server = &_sender._server,
				runner = _sender._runner,
				request = std::move(_request),
				handleAllErrors = _handleAllErrors,
				done = std::move(_done),
				fail = std::move(_fail)
			]() mutable {
				if (state->cancelled) {
					return;
				}
				try {
					auto result = server->execute(request);
					if (done && !state->detached) {
						runner([state, done = std::move(done), result]() mutable {
							if (!state->cancelled && !state->detached) {
								done(std::move(result));
							}
						});
					}
				} catch (const Error &error) {
					if (!handleAllErrors
						&& (error.name.starts_with("FLOOD_WAIT_")
							|| error.status >= 500 || error.status < 0)) {
						++server->suppressedDefaultErrors;
						return;
					}
					if (fail && !state->detached) {
						runner([state, fail = std::move(fail), error]() mutable {
							if (!state->cancelled && !state->detached) {
								fail(error);
							}
						});
					}
				}
			};
			if (_sender._server.defer) {
				_sender._server.pending.push_back(std::move(perform));
			} else {
				perform();
			}
			return id;
		}

	private:
		ConcurrentSender &_sender;
		Request _request;
		Fn<void(Response)> _done;
		Fn<void(const Error &)> _fail;
		bool _handleAllErrors = false;

	};

	template <typename Request>
	SpecificRequestBuilder<Request> request(Request request) {
		return { *this, std::move(request) };
	}

	struct DetachedRequest {
		std::shared_ptr<RequestState> state;
		void detach() { if (state) state->detached = true; }
		void cancel() { if (state) state->cancelled = true; }
	};

	DetachedRequest request(mtpRequestId id) {
		const auto i = _requests.find(id);
		return { i == _requests.end() ? nullptr : i->second };
	}

private:
	Server &_server;
	Fn<void(FnMut<void()>)> _runner = [](FnMut<void()> callback) { callback(); };
	std::map<mtpRequestId, std::shared_ptr<RequestState>> _requests;
	mtpRequestId _nextRequestId = 1;

};

}

#ifdef TAKEOUT_SHARED_API
#include "export/export_takeout_session.h"
#endif

namespace Export {

class ApiWrap {
public:
#ifdef TAKEOUT_SHARED_API
	ApiWrap(
		MTP::Server &server,
		Settings settings,
		base::weak_qptr<TakeoutSession> session,
		Fn<void(FnMut<void()>)> runner = [](FnMut<void()> callback) { callback(); })
	: _takeoutSession(session)
	, _takeoutLease(std::make_shared<TakeoutSession::Lease>())
	, _settings(std::make_unique<Settings>(settings)) {
		_runner = [
			weak = std::weak_ptr<int>(_callbackLifetime),
			runner = std::move(runner)
		](FnMut<void()> callback) {
			runner([weak, callback = std::move(callback)]() mutable {
				if (!weak.expired()) {
					callback();
				}
			});
		};
		_mtp = std::make_unique<MTP::ConcurrentSender>(&server, _runner);
	}
	~ApiWrap();
#else
	ApiWrap(MTP::Server &server, Settings settings)
	: _mtp(server), _settings(std::make_unique<Settings>(settings)) {
	}
#endif

	void startMainSession(FnMut<void()> done);
	rpl::producer<bool> waitingForTakeout() const;
	void finishExport(FnMut<void()> done);
	void cancelExportFast();
	bool probe();
	void requestProbe(FnMut<void()> done);
	std::optional<uint64> takeoutId() const { return _takeoutId; }
	std::vector<std::string> errors;

private:
	template <typename Request>
	class RequestBuilder;
	template <typename Request>
	auto mainRequest(Request &&request);
#ifdef TAKEOUT_SHARED_API
	void error(const MTP::Error &value);
	void error(const QString &value) { error(MTP::Error{ value }); }
	struct ErrorStream {
		std::vector<std::string> &values;
		void fire_copy(const MTP::Error &error) { values.push_back(error.name); }
	};
	ErrorStream _errors = { errors };
	rpl::event_stream<bool> _waitingForTakeoutChanges;
	std::unique_ptr<MTP::ConcurrentSender> _mtp;
	base::weak_qptr<TakeoutSession> _takeoutSession;
	Fn<void(FnMut<void()>)> _runner;
	std::shared_ptr<TakeoutSession::Lease> _takeoutLease;
	std::shared_ptr<int> _callbackLifetime = std::make_shared<int>(0);
	bool _cancelled = false;
#else
	void error(const MTP::Error &value) { errors.push_back(value.name); }
	void error(const QString &value) { errors.push_back(value); }
	MTP::ConcurrentSender _mtp;
#endif
	std::optional<uint64> _takeoutId;
	std::optional<UserId> _selfId;
	std::unique_ptr<Settings> _settings;

};

}
