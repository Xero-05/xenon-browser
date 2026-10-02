#include "xenon/file_policy.hpp"
#include "xenon/local_security.hpp"
#include <windows.h>
#include <winioctl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace xenon;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const char* data) { std::ofstream file(path); file << data; }
void require_owner_inheritance(const std::filesystem::path& path, bool inherited, bool directory, bool extra_capability = false) {
  PACL acl{}; PSECURITY_DESCRIPTOR descriptor{}; PSID owner{}, capability{};
  require(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
    nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS, "Cannot inspect synthetic file ACL");
  require(ConvertStringSidToSidW(local_security::current_user_sid().c_str(), &owner), "Cannot parse synthetic owner SID");
  if (extra_capability) require(ConvertStringSidToSidW(L"S-1-15-3-1", &capability), "Cannot parse synthetic capability SID");
  bool found = false, effective = false, propagates = false;
  require(acl && acl->AceCount > 0, "Current-user ACL is empty");
  for (DWORD index = 0; index < acl->AceCount; ++index) {
    void* raw{}; require(GetAce(acl, index, &raw), "Cannot inspect inherited ACE");
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
    require(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE, "Unexpected access control entry type");
    if (!EqualSid(owner, const_cast<DWORD*>(&ace->SidStart))) {
      require(capability && EqualSid(capability, const_cast<DWORD*>(&ace->SidStart)), "Owner-only policy added an unexpected principal");
      continue;
    }
    found = true;
    require(!!(ace->Header.AceFlags & INHERITED_ACE) == inherited, "Current-user ACE did not inherit correctly");
    if (!(ace->Header.AceFlags & INHERIT_ONLY_ACE)) effective = true;
    if ((ace->Header.AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) propagates = true;
  }
  LocalFree(owner); if (capability) LocalFree(capability); LocalFree(descriptor);
  require(found, "Current user lost access to a descendant");
  require(effective && propagates == directory, "Current-user effective/inheritable access does not match the object policy");
}
void capability_inheritance_regression(const std::filesystem::path& root) {
  const auto parent = root / "owner-inheritance";
  const auto network = parent / "workspace" / "Network";
  std::filesystem::create_directories(parent);
  local_security::restrict_path(parent);
  require_owner_inheritance(parent, false, true);
  std::filesystem::create_directories(network);
  require_owner_inheritance(network, true, true);
  PACL old_acl{}, replacement{}; PSECURITY_DESCRIPTOR descriptor{}; PSID capability{};
  require(GetNamedSecurityInfoW(const_cast<LPWSTR>(network.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
    nullptr, nullptr, &old_acl, nullptr, &descriptor) == ERROR_SUCCESS, "Cannot inspect synthetic network ACL");
  require(ConvertStringSidToSidW(L"S-1-15-3-1", &capability), "Cannot create synthetic capability SID");
  EXPLICIT_ACCESSW access{};
  access.grfAccessPermissions = FILE_ALL_ACCESS;
  access.grfAccessMode = GRANT_ACCESS;
  access.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE;
  access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
  access.Trustee.ptstrName = static_cast<LPWSTR>(capability);
  require(SetEntriesInAclW(1, &access, old_acl, &replacement) == ERROR_SUCCESS, "Cannot construct synthetic sandbox ACL");
  require(SetNamedSecurityInfoW(const_cast<LPWSTR>(network.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
    nullptr, nullptr, replacement, nullptr) == ERROR_SUCCESS, "Cannot apply synthetic sandbox ACL");
  LocalFree(replacement); LocalFree(descriptor); LocalFree(capability);
  const auto data = network / "nested" / "synthetic-cookie.txt";
  std::filesystem::create_directory(data.parent_path());
  write(data, "synthetic persistent data");
  std::ifstream read(data); std::string contents; std::getline(read, contents);
  require(contents == "synthetic persistent data", "Sandbox-created descendant lost current-user read access");
  require_owner_inheritance(data, true, false, true);
  read.close();
  local_security::restrict_path(data);
  require_owner_inheritance(data, false, false);
}
struct Scratch {
  std::filesystem::path parent = std::filesystem::absolute(std::filesystem::current_path() / "test_state");
  std::filesystem::path root = parent / ("files-" + local_security::random_hex(8));
  Scratch() { std::filesystem::create_directories(root); }
  ~Scratch() { if (root.parent_path() == parent && root.filename().wstring().starts_with(L"files-")) { std::error_code ec; std::filesystem::remove_all(root, ec); } }
};
void junction(const std::filesystem::path& path, const std::filesystem::path& target) {
  struct MountPoint {
    ULONG tag; USHORT length, reserved;
    USHORT substitute_offset, substitute_length, print_offset, print_length;
    wchar_t paths[1];
  };
  const auto substitute = L"\\??\\" + target.native();
  const auto print = target.native();
  const size_t path_bytes = (substitute.size() + print.size() + 2) * sizeof(wchar_t);
  std::vector<unsigned char> buffer(offsetof(MountPoint, paths) + path_bytes);
  auto* data = reinterpret_cast<MountPoint*>(buffer.data());
  data->tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->length = static_cast<USHORT>(8 + path_bytes);
  data->substitute_offset = 0; data->substitute_length = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
  data->print_offset = static_cast<USHORT>((substitute.size() + 1) * sizeof(wchar_t));
  data->print_length = static_cast<USHORT>(print.size() * sizeof(wchar_t));
  memcpy(data->paths, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
  memcpy(reinterpret_cast<unsigned char*>(data->paths) + data->print_offset, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
  std::filesystem::create_directory(path);
  const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not prepare junction test");
  DWORD returned{};
  const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
  CloseHandle(handle);
  require(success, "Could not create junction test");
}
}
int main() {
  try {
    Scratch scratch;
    capability_inheritance_regression(scratch.root);
    const auto state = scratch.root / "profile";
    const auto inputs = scratch.root / "inputs";
    std::filesystem::create_directories(state);
    std::filesystem::create_directories(inputs);
    write(state / "vault.sqlite", "secret state");
    write(inputs / "report.txt", "user granted content");
    write(inputs / "passwords.csv", "credential export");
    FilePolicy files(scratch.root / "downloads", {state});
    auto grant = files.grant_upload("workspace-a", inputs / "report.txt");
    require(grant.at("ok"), "Ordinary file grant failed");
    require(!grant.at("result").contains("path"), "File grant exposed local path");
    const auto id = grant.at("result").at("fileId").get<std::string>();
    require(!files.resolve_upload("workspace-b", id), "Cross-workspace upload accepted");
    require(!files.resolve_upload("workspace-a", "invented"), "Unknown file handle accepted");
    {
      auto resolved = files.resolve_upload("workspace-a", id);
      require(resolved.has_value(), "Valid upload could not be resolved");
      require(resolved->path() == inputs / "report.txt", "Wrong upload path");
      const auto changed = CreateFileW((inputs / "report.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
      require(changed == INVALID_HANDLE_VALUE, "Resolved upload permitted content replacement");
      if (changed != INVALID_HANDLE_VALUE) CloseHandle(changed);
      require(!MoveFileW(inputs.c_str(), (scratch.root / "moved-inputs").c_str()), "Resolved upload permitted ancestor replacement");
    }
    require(!files.grant_upload("workspace-a", state / "vault.sqlite").at("ok"), "Protected file grant accepted");
    files.deny_source(inputs / "passwords.csv");
    require(!files.grant_upload("workspace-a", inputs / "passwords.csv").at("ok"), "Source credential CSV accepted");
    std::filesystem::rename(inputs / "passwords.csv", inputs / "renamed-export.csv");
    require(!files.grant_upload("workspace-a", inputs / "renamed-export.csv").at("ok"), "Renamed source credential CSV accepted");
    {
      FilePolicy reopened(scratch.root / "downloads", {state});
      require(reopened.resolve_upload("workspace-a", id).has_value(), "Approved single-file grant did not persist");
      require(!reopened.grant_upload("workspace-a", inputs / "renamed-export.csv").at("ok"), "Sensitive source denial did not persist");
      require(!reopened.grant_upload("workspace-a", scratch.root / "downloads" / ".policy" / "sensitive-inputs.json").at("ok"), "Private file policy could be uploaded");
    }
    require(!files.grant_upload("workspace-a", inputs / ".." / "profile" / "vault.sqlite").at("ok"), "Path traversal accepted");
    require(!files.grant_upload("workspace-a", std::filesystem::path((inputs / "report.txt").native() + L":hidden")).at("ok"), "Alternate data stream accepted");
    require(!files.grant_upload("workspace-a", L"\\\\localhost\\C$\\file.txt").at("ok"), "UNC file accepted");
    require(!files.grant_upload("workspace-a", L"\\\\?\\C:\\file.txt").at("ok"), "Device path accepted");
    const auto alias = inputs / "alias.txt";
    require(CreateHardLinkW(alias.c_str(), (state / "vault.sqlite").c_str(), nullptr), "Hardlink fixture failed");
    require(!files.grant_upload("workspace-a", alias).at("ok"), "Protected file hardlink bypass accepted");
    const auto link = scratch.root / "linked-profile";
    junction(link, state);
    require(!files.grant_upload("workspace-a", link / "vault.sqlite").at("ok"), "Junction escape accepted");
    require(RemoveDirectoryW(link.c_str()), "Junction fixture cleanup failed");
    std::filesystem::create_directories(inputs / "nested");
    write(inputs / "nested" / "notes.txt", "Nested approved file");
    constexpr const char* pairing = "{\"clientId\":\"synthetic-file-policy-client\",\"token\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"pipe\":\"\\\\\\\\.\\\\pipe\\\\xenon-browser\"}";
    write(inputs / "ordinary-report.txt", pairing);
    write(inputs / "Native-Client-Config.json", "{}");
    write(inputs / "saved.clientconfig", "{}");
    { std::ofstream large(inputs / "oversize-object.txt", std::ios::binary); large << '{' << std::string(1024 * 1024, ' ') << "\"clientId\":\"beyond-probe\"}"; }
    require(!files.grant_upload("workspace-a", inputs / "ordinary-report.txt").at("ok"), "Arbitrarily named connection credentials were approved");
    require(!files.grant_upload("workspace-a", inputs / "Native-Client-Config.json").at("ok"), "Recognized connection filename was approved");
    require(!files.grant_upload("workspace-a", inputs / "saved.clientconfig").at("ok"), "Clientconfig filename was approved");
    require(!files.grant_upload("workspace-a", inputs / "oversize-object.txt").at("ok"), "Oversize JSON object bypassed bounded credential inspection");
    write(inputs / "changeable.txt", "An initially ordinary approved file");
    const auto mutable_grant = files.grant_upload("workspace-a", inputs / "changeable.txt");
    require(mutable_grant.at("ok"), "Mutable fixture could not be approved");
    write(inputs / "changeable.txt", pairing);
    require(!files.resolve_upload("workspace-a", mutable_grant.at("result").at("fileId").get<std::string>()), "Same-identity approved file became transferable connection credentials");
    const auto singles = files.list_files("workspace-a");
    require(singles.at("result").at("files").size() == 1, "Single-file listing exposed a connection config after modification");
    const auto folder_grant = files.grant_folder("workspace-a", inputs);
    require(folder_grant.at("ok"), "Native folder grant failed");
    const auto folder_id = folder_grant.at("result").at("folderId").get<std::string>();
    require(files.list_folders("workspace-b").at("result").at("folders").empty(), "Folder grant crossed workspace boundary");
    require(!files.list_files("workspace-b", folder_id).at("ok"), "Another workspace listed an approved folder");
    const auto folder_contents = files.list_files("workspace-a", folder_id);
    require(folder_contents.at("ok") && folder_contents.at("result").at("files").size() == 2, "Folder listing did not exclude sensitive exports, pairing configs and hardlinks");
    require(folder_contents.dump().find(scratch.root.string()) == std::string::npos, "Folder listing leaked absolute directory");
    require(files.list_files("workspace-a", folder_id, 1).at("result").at("truncated"), "Bounded folder listing did not report truncation");
    const auto folder_file_id = folder_contents.at("result").at("files").at(0).at("fileId").get<std::string>();
    {
      FilePolicy reopened(scratch.root / "downloads", {state});
      require(reopened.list_folders("workspace-a").at("result").at("folders").size() == 1, "Folder grant did not persist");
      require(reopened.resolve_upload("workspace-a", folder_file_id).has_value(), "Derived folder file handle did not persist");
    }
    require(files.revoke_grant("workspace-a", folder_id).at("result").at("revoked"), "Native folder revocation failed");
    require(!files.resolve_upload("workspace-a", folder_file_id), "Folder revocation left a derived file capability");
    require(!files.list_files("workspace-a", folder_id).at("ok"), "Revoked folder could still be listed");
    {
      FilePolicy reopened(scratch.root / "downloads", {state});
      require(reopened.list_folders("workspace-a").at("result").at("folders").empty(), "Folder revocation did not persist");
    }
    auto parent_grant = files.grant_folder("workspace-a", scratch.root);
    require(parent_grant.at("ok"), "An approved parent folder could not be granted");
    const auto broad = files.list_files("workspace-a", parent_grant.at("result").at("folderId").get<std::string>());
    for (const auto& file : broad.at("result").at("files")) {
      const auto relative = file.at("relativePath").get<std::string>();
      require(!relative.starts_with("profile") && relative.find(".policy") == std::string::npos && relative.find("renamed-export") == std::string::npos,
              "Approved ancestor exposed a protected descendant");
    }
    std::filesystem::remove(inputs / "report.txt");
    write(inputs / "report.txt", "replacement identity");
    require(!files.resolve_upload("workspace-a", id), "Replaced grant file accepted");
    auto replacement = files.grant_upload("workspace-a", inputs / "report.txt");
    require(replacement.at("ok"), "New file could not be regranted");
    files.revoke_scope("workspace-a");
    require(!files.resolve_upload("workspace-a", replacement.at("result").at("fileId").get<std::string>()), "Revoked scope still resolved");
    for (const std::string name : {"../../escape.txt", "CON", "NUL.txt", "COM1.exe", "file.txt:secret", "x. ", "", "a/b\\c.txt"}) {
      const auto safe = FilePolicy::safe_download_name(name);
      require(!safe.empty() && safe.find_first_of("/\\:< >|?*") == std::string::npos, "Unsafe download filename");
      require(safe.back() != '.' && safe.back() != ' ', "Trailing Windows filename ambiguity");
    }
    require(FilePolicy::safe_download_name("CON") == "download-CON", "Reserved name not neutralized");
    const auto first = files.allocate_download("workspace-a", "../../escape.txt");
    const auto second = files.allocate_download("workspace-a", "../../escape.txt");
    require(first != second && first.parent_path() == second.parent_path(), "Download names not uniquely scoped");
    require(first.parent_path().parent_path() == scratch.root / "downloads", "Download escaped its managed root");
    std::cout << "Persistent file/folder capability, revocation, identity, path, junction and download tests passed.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "File policy test failed: " << error.what() << '\n'; return 1; }
}
