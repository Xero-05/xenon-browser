#include "xenon/vault.hpp"
#include "xenon/local_security.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace xenon;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& data) { std::ofstream file(path, std::ios::binary); file << data; }
std::string read(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
struct Scratch {
  std::filesystem::path parent = std::filesystem::absolute(std::filesystem::current_path() / "test_state");
  std::filesystem::path root = parent / ("vault-" + local_security::random_hex(8));
  Scratch() { std::filesystem::create_directories(root); }
  ~Scratch() { if (root.parent_path() == parent && root.filename().wstring().starts_with(L"vault-")) { std::error_code ec; std::filesystem::remove_all(root, ec); } }
};
std::string error_code(const Json& value) { return value.at("error").at("code").get<std::string>(); }
void pending_credentials(const std::filesystem::path& root) {
  const auto path = root / "pending" / "vault.sqlite";
  const std::string origin = "https://pending.test";
  const std::string user = "PENDING_USERNAME_CANARY_21588@example.test";
  const std::string password = "PENDING_PASSWORD_CANARY_48561!";
  std::string id;
  {
    Vault vault(path);
    require(!vault.propose_login("http://pending.test", user, password), "Insecure login produced a prompt");
    require(!vault.propose_login(origin, user, ""), "Empty password produced a prompt");
    auto proposal = vault.propose_login(origin + "/login", user, password);
    require(proposal && proposal->origin == origin && !proposal->update && proposal->username_label == "********", "New login metadata incorrect");
    require(proposal->candidate_id.size() == 32 && proposal->candidate_id != user, "Proposal needs an opaque token");
    require(vault.pending_logins().size() == 1, "Pending login missing");
    require(vault.list_accounts().at("result").at("accounts").empty(), "Proposal persisted an account before native confirmation");
    require(read(path).find(user) == std::string::npos && read(path).find(password) == std::string::npos && read(path).find(origin) == std::string::npos,
            "Pending credential or metadata persisted before confirmation");
    const auto duplicate = vault.propose_login(origin, user, password);
    require(duplicate && duplicate->candidate_id == proposal->candidate_id && vault.pending_logins().size() == 1, "Duplicate submission produced another prompt");
    require(error_code(vault.accept_login(proposal->candidate_id, "invalid\nlabel")) == "invalid_credential", "Invalid label accepted");
    require(vault.pending_logins().size() == 1, "Correctable label error consumed a prompt");
    const auto saved = vault.accept_login(proposal->candidate_id, "Personal account");
    require(saved.at("ok") && saved.at("result").at("status") == "created", "Native confirmation did not save");
    id = saved.at("result").at("accountId").get<std::string>();
    require(saved.dump().find(user) == std::string::npos && saved.dump().find(password) == std::string::npos, "Confirmation response exposed secret");
    const auto secret = vault.get_secret(id, origin);
    require(secret && secret->username() == user && secret->password() == password, "Confirmed encrypted candidate did not round trip");
    require(error_code(vault.accept_login(proposal->candidate_id)) == "candidate_unavailable", "Confirmation token could be replayed");
    require(!vault.propose_login(origin, user, password), "Unchanged login was not suppressed");
    auto update = vault.propose_login(origin, user, password + "-new");
    require(update && update->update, "Changed password did not propose update");
    auto newer = vault.propose_login(origin, user, password + "-newer");
    require(newer && newer->update && newer->candidate_id != update->candidate_id && vault.pending_logins().size() == 1, "New submission did not supersede prior account prompt");
    require(error_code(vault.accept_login(update->candidate_id)) == "candidate_unavailable", "Superseded prompt could overwrite a newer submission");
    require(vault.accept_login(newer->candidate_id).at("result").at("status") == "updated", "Update confirmation failed");
    require(vault.get_secret(id, origin)->password() == password + "-newer", "Confirmed update did not persist");
    require(vault.list_accounts().at("result").at("accounts").at(0).at("label") == "Personal account", "Update discarded native account label");
    auto obsolete = vault.propose_login(origin, user, password);
    require(obsolete && !vault.propose_login(origin, user, password + "-newer") && vault.pending_logins().empty(), "Unchanged submission left an obsolete account prompt");
    auto other_origin = vault.propose_login("https://pending.test:8443", user, password);
    require(other_origin && !other_origin->update, "Different HTTPS port reused an existing account");
    vault.dismiss_login(other_origin->candidate_id);
    require(vault.pending_logins().empty() && error_code(vault.accept_login(other_origin->candidate_id)) == "candidate_unavailable", "Dismissed candidate remained usable");
    const auto empty_user = vault.propose_login("https://empty-user.test", "", password);
    require(empty_user && empty_user->username_label == "(empty username)", "Empty username display incorrect");
    const auto empty_saved = vault.accept_login(empty_user->candidate_id);
    require(empty_saved.at("ok") && vault.get_secret(empty_saved.at("result").at("accountId"), "https://empty-user.test")->username().empty(), "Empty username credential did not round trip");
    require(vault.propose_login("https://not-persisted.test", user, password).has_value(), "Ephemeral proposal unavailable");
  }
  const auto disk = read(path);
  require(disk.find(user) == std::string::npos && disk.find(password) == std::string::npos, "Confirmed pending credential persisted in plaintext");
  Vault reopened(path);
  require(reopened.pending_logins().empty(), "Pending prompts survived process lifetime");
  require(reopened.get_secret(id, origin)->password() == password + "-newer", "Confirmed credential not durable");
  require(reopened.list_accounts("https://not-persisted.test").at("result").at("accounts").empty(), "Unconfirmed credential was persisted");
}
void pending_conflicts(const std::filesystem::path& root) {
  const auto path = root / "pending-conflicts" / "vault.sqlite";
  Vault vault(path), other(path);
  const std::string origin = "https://stale.test", user = "stale-user";
  auto create = vault.propose_login(origin, user, "first");
  require(create.has_value(), "New stale fixture proposal missing");
  const auto saved = other.save(origin, user, "first", "Original");
  const auto id = saved.at("result").at("accountId").get<std::string>();
  require(error_code(vault.accept_login(create->candidate_id)) == "candidate_stale", "Save prompt overwrote a later account creation");
  require(vault.pending_logins().empty(), "Stale confirmation was not consumed");
  auto update = vault.propose_login(origin, user, "candidate");
  require(update && other.save(origin, user, "intermediate", "Original", id).at("ok"), "Stale update fixture setup failed");
  require(error_code(vault.accept_login(update->candidate_id)) == "candidate_stale" && vault.get_secret(id, origin)->password() == "intermediate", "Stale update overwrote another native writer");
  update = vault.propose_login(origin, user, "candidate");
  require(other.save(origin, user, "roundtrip", "Original", id).at("ok") && other.save(origin, user, "intermediate", "Original", id).at("ok"), "ABA fixture failed");
  require(error_code(vault.accept_login(update->candidate_id)) == "candidate_stale", "Changed-and-restored record bypassed encrypted revision check");
  update = vault.propose_login(origin, user, "candidate");
  require(other.save(origin, user, "intermediate", "Renamed", id).at("ok"), "Rename fixture failed");
  require(error_code(vault.accept_login(update->candidate_id)) == "candidate_stale", "Old prompt overwrote a renamed account");
  update = vault.propose_login(origin, user, "candidate");
  require(other.remove(id).at("ok"), "Delete fixture failed");
  require(error_code(vault.accept_login(update->candidate_id)) == "candidate_stale", "Old update recreated a removed account");
  require(vault.list_accounts(origin).at("result").at("accounts").empty(), "Stale update mutated storage");
}
void pending_lifetime(const std::filesystem::path& root) {
  auto now = std::chrono::steady_clock::time_point{};
  Vault vault(root / "pending-lifetime" / "vault.sqlite", [&] { return now; });
  auto expiring = vault.propose_login("https://expiry.test", "user", "password");
  require(expiring.has_value(), "Expiry fixture failed");
  now += Vault::kPendingLoginLifetime - std::chrono::seconds(1);
  require(vault.pending_logins().size() == 1, "Pending login expired too soon");
  require(vault.propose_login("https://expiry.test", "user", "password")->candidate_id == expiring->candidate_id, "Identical login changed pending token");
  now += std::chrono::seconds(1);
  require(error_code(vault.accept_login(expiring->candidate_id)) == "candidate_unavailable", "Repeated proposal extended expiry or accept ignored expiry");
  expiring = vault.propose_login("https://expiry.test", "user", "password");
  now += Vault::kPendingLoginLifetime;
  require(vault.pending_logins().empty(), "Pending list did not expire candidates");
  std::vector<std::string> tokens;
  for (size_t i = 0; i <= Vault::kMaxPendingLogins; ++i) {
    const auto candidate = vault.propose_login("https://bound-" + std::to_string(i) + ".test", "user", "password");
    require(candidate.has_value(), "Bounded queue rejected valid proposal");
    tokens.push_back(candidate->candidate_id);
  }
  require(vault.pending_logins().size() == Vault::kMaxPendingLogins, "Pending queue exceeded bound");
  require(error_code(vault.accept_login(tokens.front())) == "candidate_unavailable", "Queue did not evict oldest pending login");
  require(vault.accept_login(tokens.back()).at("ok"), "Queue eviction damaged retained encrypted candidate");
  vault.set_locked(true);
  require(vault.pending_logins().empty(), "Lock did not clear pending candidates");
  require(!vault.propose_login("https://locked.test", "user", "password"), "Locked vault accepted proposal");
  require(error_code(vault.accept_login(tokens[1])) == "vault_locked", "Locked vault accepted native confirmation");
  vault.set_locked(false);
  require(vault.pending_logins().empty() && error_code(vault.accept_login(tokens[1])) == "candidate_unavailable", "Unlock revived a discarded candidate");
}
}
int main() {
  try {
    Scratch scratch;
    pending_credentials(scratch.root);
    pending_conflicts(scratch.root);
    pending_lifetime(scratch.root);
    const auto db = scratch.root / "state" / "vault.sqlite";
    const std::string user = "VAULT_USERNAME_CANARY_95739@example.test";
    const std::string password = "VAULT_PASSWORD_CANARY_73927!";
    std::string id;
    {
      Vault vault(db);
      require(Vault::normalize_https_origin("HTTPS://Example.COM:443/login?next=x") == "https://example.com", "Origin normalization failed");
      require(Vault::normalize_https_origin("https://example.com:8443/login") == "https://example.com:8443", "Non-default port was lost");
      require(Vault::normalize_https_origin("https://[::1]/login") == "https://[::1]", "IPv6 origin failed");
      for (const std::string bad : {"http://example.com", "https://user:pass@example.com", "https://example.com\\evil", "https://127.1", "https://0x7f000001", "file:///C:/vault", "https://example.com./", "https://example.com:0/", "https://example.com:65537/", "https://example.com:443junk/", "https://evil\n.example.com"})
        require(!Vault::normalize_https_origin(bad), "Unsafe or ambiguous origin accepted");
      auto saved = vault.save("https://example.test/login", user, password, "Work account");
      require(saved.at("ok"), "DPAPI credential save failed");
      id = saved.at("result").at("accountId").get<std::string>();
      auto secret = vault.get_secret(id, "https://example.test");
      require(secret && secret->username() == user && secret->password() == password, "DPAPI round trip failed");
      require(!vault.get_secret(id, "https://evil.example.test"), "Cross-origin credential read accepted");
      require(!vault.get_secret(id, "https://example.test:8443"), "Wrong-port credential read accepted");
      require(vault.save("https://example.test", user, password).at("result").at("status") == "unchanged", "Duplicate was not deduplicated");
      require(!vault.save("https://example.test", user, "changed").at("ok"), "Conflict overwrote a credential without confirmation");
      require(vault.save("https://example.test", user, "changed", "Work account", id).at("ok"), "Explicit password update failed");
      require(vault.get_secret(id, "https://example.test")->password() == "changed", "Password update not persisted");
      const auto metadata = vault.list_accounts().dump();
      require(metadata.find(user) == std::string::npos && metadata.find(password) == std::string::npos, "Metadata exposed a credential");
      require(vault.list_accounts("https://other.test").at("result").at("accounts").empty(), "Origin metadata filtering failed");
      vault.set_locked(true);
      require(!vault.get_secret(id, "https://example.test"), "Locked vault returned secret");
      require(!vault.save("https://example.test", "new", "password").at("ok"), "Locked vault accepted save");
      require(!vault.remove(id).at("ok"), "Locked vault accepted removal");
      vault.set_locked(false);
      require(vault.save("https://example.test", user, password, "Work account", id).at("ok"), "Restore failed");
    }
    const auto disk = read(db);
    require(disk.find(user) == std::string::npos && disk.find(password) == std::string::npos, "SQLite contains plaintext credential canary");
    {
      Vault vault(db);
      require(vault.get_secret(id, "https://example.test")->password() == password, "Reopened vault did not decrypt");
      const auto chrome = scratch.root / "chrome.csv";
      write(chrome, "\xEF\xBB\xBF" "name,url,username,password,note\r\n"
                    "Example,https://csv.test/login,\"csv,user\",\"canary\"\"quoted\r\nmultiline\",\"ignore\"\r\n"
                    "Duplicate,https://csv.test/,\"csv,user\",\"canary\"\"quoted\r\nmultiline\",\r\n"
                    "Unsafe,http://insecure.test,user,secret,\r\n");
      const auto preview = vault.preview_csv(chrome);
      require(preview.at("ok") && preview.at("result").at("validRows") == 2 && preview.at("result").at("invalidRows") == 1, "Native CSV preview counts wrong");
      require(preview.dump().find("csv,user") == std::string::npos && preview.dump().find("canary") == std::string::npos, "Preview exposed credential values");
      require(vault.list_accounts("https://csv.test").at("result").at("accounts").empty(), "Preview mutated the vault");
      auto imported = vault.import_csv(chrome, false, preview.at("result").at("fileDigest").get<std::string>());
      require(imported.at("ok"), "Chrome multiline CSV failed");
      require(imported.at("result").at("created") == 1 && imported.at("result").at("unchanged") == 1 && imported.at("result").at("invalid") == 1, "CSV import counts wrong");
      const auto csv_id = vault.list_accounts("https://csv.test").at("result").at("accounts").at(0).at("accountId").get<std::string>();
      require(vault.get_secret(csv_id, "https://csv.test")->password() == "canary\"quoted\r\nmultiline", "Quoted password bytes changed");
      const auto firefox = scratch.root / "firefox.csv";
      write(firefox, "\"url\",\"username\",\"password\",\"httpRealm\",\"formActionOrigin\",\"guid\",\"timeCreated\",\"timeLastUsed\",\"timePasswordChanged\"\n"
                     "\"https://firefox.test\",\"ffuser\",\"ffpassword\",\"\",\"https://firefox.test\",\"x\",\"1\",\"1\",\"1\"\n");
      require(vault.import_csv(firefox).at("result").at("created") == 1, "Firefox CSV failed");
      const auto edge = scratch.root / "edge.csv";
      write(edge, "name,url,username,password\nCSV,https://csv.test,\"csv,user\",replacement\n");
      const auto conflict_preview = vault.preview_csv(edge);
      require(conflict_preview.at("result").at("conflicts") == 1, "Preview omitted saved password conflict");
      require(vault.import_csv(edge).at("result").at("conflicts") == 1, "Import silently resolved conflict");
      require(vault.get_secret(csv_id, "https://csv.test")->password() != "replacement", "Conflict changed vault");
      require(vault.import_csv(edge, true).at("result").at("updated") == 1, "Explicit import conflict replacement failed");
      require(vault.get_secret(csv_id, "https://csv.test")->password() == "replacement", "Explicit import did not update secret");
      write(edge, "url,username,password\nhttps://preview-change.test,user,first\n");
      const auto before_change = vault.preview_csv(edge);
      write(edge, "url,username,password\nhttps://preview-change.test,user,second\n");
      require(!vault.import_csv(edge, false, before_change.at("result").at("fileDigest").get<std::string>()).at("ok"), "Changed CSV imported after preview");
      require(vault.list_accounts("https://preview-change.test").at("result").at("accounts").empty(), "Changed CSV partially imported");
      write(edge, "url,username,password\nhttps://valid.test,x,good\nhttps://broken.test,x,\"unfinished");
      require(!vault.import_csv(edge).at("ok"), "Malformed CSV accepted");
      require(vault.list_accounts("https://valid.test").at("result").at("accounts").empty(), "Malformed import partially committed");
      write(edge, "url,username,password,password\nhttps://bad.test,x,one,two\n");
      require(!vault.import_csv(edge).at("ok"), "Duplicate columns accepted");
      write(edge, "url,username,password\nhttps://bad.test,x,\"quoted\"junk\n");
      require(!vault.import_csv(edge).at("ok"), "Malformed post-quote CSV accepted");
      require(vault.remove(id).at("result").at("removed"), "Remove failed");
      require(!vault.get_secret(id, "https://example.test"), "Removed credential still available");
      require(std::filesystem::exists(chrome), "Importer unexpectedly deleted source");
    }
    std::cout << "Vault DPAPI, pending login approval, encrypted storage, origin and browser CSV tests passed.\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "Vault test failed: " << error.what() << '\n'; return 1; }
}
