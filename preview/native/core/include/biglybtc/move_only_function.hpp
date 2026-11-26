// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>

#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
#ifdef __APPLE__
#include <boost/compat/move_only_function.hpp>
#else
#error BiglyBTC requires the qualified C++23 move_only_function facility
#endif
#endif

namespace biglybtc::core {

template<class Signature>
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using MoveOnlyFunction = std::move_only_function<Signature>;
#else
using MoveOnlyFunction = boost::compat::move_only_function<Signature>;
#endif

} // namespace biglybtc::core
