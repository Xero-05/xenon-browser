// Test-only helper. Never packaged with Xenon. Seeds only a marked, new,
// disposable integration profile with public synthetic canary credentials.
#include "xenon/vault.hpp"
#include "xenon/file_policy.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: vault_fixture_seed <new-auth-integration-profile>");
    const auto profile = std::filesystem::absolute(argv[1]).lexically_normal();
    if (profile.parent_path().filename() != L".cache" || !profile.filename().wstring().starts_with(L"auth-integration-"))
      throw std::runtime_error("Seed helper accepts only a disposable auth-integration profile");
    std::ifstream marker(profile / L"SYNTHETIC_TEST_PROFILE", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(marker), {}};
    if (contents != "XENON_SYNTHETIC_AUTH_FIXTURE\n") throw std::runtime_error("Test profile marker is missing");
    const auto database = profile / L"vault.sqlite3";
    if (std::filesystem::exists(database)) throw std::runtime_error("Refusing to seed an existing vault");
    xenon::Vault vault(database);
    const std::string origin = "https://127.0.0.1:18766";
    auto result = vault.save(origin, "XENON_TEST_USERNAME_CANARY_8e9a@example.invalid",
      "XENON_TEST_PASSWORD_CANARY_73ab!", "Synthetic authentication fixture");
    if (!result.value("ok", false)) throw std::runtime_error("Synthetic fixture seed failed");
    const auto upload_root = profile.parent_path() / (profile.filename().wstring() + L"-uploads");
    std::filesystem::create_directories(upload_root);
    { std::ofstream file(upload_root / L"approved-report.txt", std::ios::binary); file << "Xenon synthetic approved upload.\n"; }
    { std::ofstream file(upload_root / L"meeting-notes.txt", std::ios::binary);
      file << R"({"clientId":"synthetic-nonfunctional-client","token":"fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210","pipe":"\\\\.\\pipe\\xenon-browser"})"; }
    xenon::FilePolicy files(profile / L"downloads", {profile});
    const auto folder = files.grant_folder("auth_fixture_shared", upload_root);
    if (!folder.value("ok", false)) throw std::runtime_error("Synthetic upload fixture grant failed");
    std::cout << xenon::Json{{"accountId", result.at("result").at("accountId")}, {"origin", origin},
                            {"folderId", folder.at("result").at("folderId")}}.dump() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
