#pragma once

#include "fake_domain.h"

namespace Export {

class TakeoutSession {
public:
	explicit TakeoutSession(Main::Transport *transport)
	: owner(transport->session) {
	}
	static base::weak_qptr<TakeoutSession> ForSession(
		not_null<Main::Session*> session);
	Main::Session *owner = nullptr;
	std::shared_ptr<int> testLifetime = std::make_shared<int>(0);

};

}
