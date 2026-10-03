#pragma once
#include <filesystem>
#include <windows.h>

namespace xenon::ui {
inline constexpr wchar_t documentation_url[]=L"https://github.com/Xero-05/xenon-browser/blob/main/docs/GETTING_STARTED.md";
enum class IntroductionResult { cancelled, start, documentation };
// Check before CEF creates profile files. Existing profiles keep ordinary startup.
bool needs_introduction(const std::filesystem::path& root);
// A fresh start selects its language before CEF initialization. Reopening the
// tour from Menu leaves the running browser's language and settings unchanged.
IntroductionResult show_introduction(HWND owner=nullptr,bool first_run=true);
}
