#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#endif

namespace xenon::local_security {
#ifdef _WIN32
inline std::wstring current_user_sid() {
  HANDLE token{};
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot query local identity");
  DWORD size{};
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> storage(size);
  if (!GetTokenInformation(token, TokenUser, storage.data(), size, &size)) { CloseHandle(token); throw std::runtime_error("Cannot query local identity"); }
  CloseHandle(token);
  LPWSTR sid{};
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) throw std::runtime_error("Cannot identify local user");
  std::wstring result(sid); LocalFree(sid); return result;
}
struct SecurityDescriptor {
  PSECURITY_DESCRIPTOR descriptor{};
  SECURITY_ATTRIBUTES attributes{};
  explicit SecurityDescriptor(bool inherit_to_children = false) {
    const auto sddl = (inherit_to_children ? L"D:P(A;OICI;GA;;;" : L"D:P(A;;GA;;;") + current_user_sid() + L")";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) throw std::runtime_error("Cannot create owner-only access policy");
    attributes = {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
  }
  ~SecurityDescriptor() { if (descriptor) LocalFree(descriptor); }
  SecurityDescriptor(const SecurityDescriptor&) = delete;
};
inline void restrict_path(const std::filesystem::path& path) {
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Cannot inspect local state access policy");
  // Chromium adds inheritable sandbox capability ACEs below its profile root.
  // Preserve the user's access to descendants it creates, without granting
  // access to any additional principal. File and pipe ACLs stay non-inheritable.
  SecurityDescriptor policy((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
  PACL acl{}; BOOL present{}, defaulted{};
  if (!GetSecurityDescriptorDacl(policy.descriptor, &present, &acl, &defaulted)) throw std::runtime_error("Cannot read owner-only access policy");
  const auto status = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr);
  if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot protect local state (Windows error " + std::to_string(status) + ")");
}
inline std::string random_hex(size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  if (BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("Secure random generator unavailable");
  constexpr char alphabet[] = "0123456789abcdef";
  std::string result; result.reserve(bytes * 2);
  for (auto value : buffer) { result.push_back(alphabet[value >> 4]); result.push_back(alphabet[value & 15]); }
  return result;
}
inline std::string sha256(const std::string& input) {
  BCRYPT_ALG_HANDLE algorithm{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("Token hashing unavailable");
  unsigned char digest[32]{};
  auto status = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status < 0) throw std::runtime_error("Token hashing unavailable");
  constexpr char alphabet[] = "0123456789abcdef";
  std::string result;
  for (auto value : digest) { result.push_back(alphabet[value >> 4]); result.push_back(alphabet[value & 15]); }
  return result;
}
#else
// This alpha's security boundary is Windows. Do not silently substitute weak
// randomness or plaintext token storage when somebody builds another platform.
inline void restrict_path(const std::filesystem::path&) { throw std::runtime_error("Windows is required"); }
inline std::string random_hex(size_t) { throw std::runtime_error("Windows is required"); }
inline std::string sha256(const std::string&) { throw std::runtime_error("Windows is required"); }
#endif
inline bool equal_secret(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char difference{};
  for (size_t i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
  return difference == 0;
}
}
