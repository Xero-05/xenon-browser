#pragma once
#include <string_view>

#ifndef XENON_VERSION
#error XENON_VERSION must come from the root VERSION file through CMake.
#endif
namespace xenon {
inline constexpr std::string_view kVersion = XENON_VERSION;
}
