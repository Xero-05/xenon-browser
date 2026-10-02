#pragma once
#include <windows.h>

namespace xenon {
// The browser implementation is a DLL loaded by CEF's sandbox bootstrap. Load
// resources from this module, not from a guessed executable or DLL filename.
inline HINSTANCE branding_module() noexcept {
  HMODULE module{};
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    reinterpret_cast<LPCWSTR>(&branding_module),&module);
  return module;
}
inline HICON branding_icon(int size) noexcept {
  const auto module=branding_module();
  if(!module||size<=0)return nullptr;
  // Resource101 is the multi-resolution Xenon app icon. Shared handles live for
  // the module's lifetime and must not be released with DestroyIcon.
  return reinterpret_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_SHARED));
}
}
