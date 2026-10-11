#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
namespace Log {
template<class... A> void Error(std::format_string<A...> fmt, A&&... args) {
    auto msg = std::format(fmt, std::forward<A>(args)...);
    OutputDebugStringA(("[Orion.Optimization] " + msg + "\n").c_str());
}
template<class... A> void Warning(std::format_string<A...> fmt, A&&... args) {
    Error(fmt, std::forward<A>(args)...);
}
}
template<class... A> [[noreturn]] void AssertFail(std::format_string<A...> fmt, A&&... args) {
    throw std::runtime_error(std::format(fmt, std::forward<A>(args)...));
}
template<class... A> void Assert(bool ok, std::format_string<A...> fmt, A&&... args) {
    if (!ok) AssertFail(fmt, std::forward<A>(args)...);
}
