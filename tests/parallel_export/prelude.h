#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#if defined(_LIBCPP_VERSION) && !defined(_LIBCPP_TEMPLATE_VIS)
#define _LIBCPP_TEMPLATE_VIS
#endif

#include <gsl/gsl>
#include <range/v3/algorithm/find_if.hpp>
#include "base/assertion.h"
#include "rpl/rpl.h"

using gsl::not_null;
using int32 = std::int32_t;
using uint64 = std::uint64_t;
using QString = std::string;

template <typename Signature>
using Fn = std::function<Signature>;

struct MsgId {
	int32 bare = 0;
};

struct MTPInputPeer {
	uint64 peerId = 0;
};

inline MTPInputPeer MTP_inputPeerEmpty() {
	return {};
}

namespace base {

template <typename Type>
Type take(Type &value) {
	return std::exchange(value, Type{});
}

namespace assertion {

inline void log(const char *message, const char *file, int line) {
	std::cerr << file << ':' << line << ": " << message << '\n';
}

}

}

namespace v {

template <typename Type, typename Variant>
bool is(const Variant &value) {
	return std::holds_alternative<Type>(value);
}

}

#define LOG(...) ((void)0)
