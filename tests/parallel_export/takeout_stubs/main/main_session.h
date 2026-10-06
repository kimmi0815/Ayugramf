#pragma once

#include "takeout_domain.h"

namespace Main {

class Session : public QObject {
public:
	class Lifetime {
	public:
		template <typename Type, typename ...Args>
		Type *make_state(Args &&...args) {
			auto result = std::make_shared<Type>(std::forward<Args>(args)...);
			const auto pointer = result.get();
			_states.push_back(std::move(result));
			return pointer;
		}

	private:
		std::vector<std::shared_ptr<QObject>> _states;

	};

	explicit Session(MTP::Server &server) : _server(server) {}
	MTP::Instance &mtp() { return _server; }
	Lifetime &lifetime() { return _lifetime; }

private:
	MTP::Server &_server;
	Lifetime _lifetime;

};

}
