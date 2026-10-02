#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include "xenon/vault.hpp"
#include "xenon/local_security.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace xenon {
namespace {
constexpr size_t kMaxField = 65536;
constexpr size_t kMaxCsv = 16 * 1024 * 1024;
void wipe(std::string& value) noexcept {
  if (!value.empty()) SecureZeroMemory(value.data(), value.size());
  value.clear();
}
struct SensitiveBytes {
  std::vector<unsigned char> value;
  SensitiveBytes() = default;
  explicit SensitiveBytes(std::vector<unsigned char> bytes) : value(std::move(bytes)) {}
  SensitiveBytes(SensitiveBytes&& other) noexcept : value(std::move(other.value)) {}
  SensitiveBytes& operator=(SensitiveBytes&& other) noexcept {
    if (this != &other) {
      if (!value.empty()) SecureZeroMemory(value.data(), value.size());
      value = std::move(other.value);
    }
    return *this;
  }
  SensitiveBytes(const SensitiveBytes&) = delete;
  SensitiveBytes& operator=(const SensitiveBytes&) = delete;
  ~SensitiveBytes() { if (!value.empty()) SecureZeroMemory(value.data(), value.size()); }
};
struct SensitiveText {
  std::string value;
  ~SensitiveText() { wipe(value); }
};
struct Rows {
  std::vector<std::vector<std::string>> value;
  ~Rows() { for (auto& row : value) for (auto& field : row) wipe(field); }
};
struct LocalBlob {
  DATA_BLOB value{};
  ~LocalBlob() {
    if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); }
  }
};
std::string utf8_path(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
bool valid_utf8(const std::string& value) {
  if (value.empty()) return true;
  if (value.size() > INT_MAX) return false;
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0) > 0;
}
std::wstring widen(const std::string& value) {
  if (!valid_utf8(value)) throw std::runtime_error("Invalid text encoding");
  const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
  return result;
}
void check_sql(int code) {
  if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW) throw std::runtime_error("Credential storage operation failed");
}
void exec_sql(sqlite3* db, const char* sql) { check_sql(sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
struct Statement {
  sqlite3_stmt* stmt{};
  Statement(sqlite3* db, const char* sql) { check_sql(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr)); }
  ~Statement() { if (stmt) sqlite3_finalize(stmt); }
  void text(int index, const std::string& value) { check_sql(sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)); }
  void bytes(int index, const std::vector<unsigned char>& value) { check_sql(sqlite3_bind_blob(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)); }
  bool next() {
    const auto code = sqlite3_step(stmt); check_sql(code); return code == SQLITE_ROW;
  }
  std::string text(int column) const {
    const auto* data = sqlite3_column_text(stmt, column);
    return data ? std::string(reinterpret_cast<const char*>(data), sqlite3_column_bytes(stmt, column)) : std::string{};
  }
};
struct Transaction {
  sqlite3* db;
  bool committed{};
  explicit Transaction(sqlite3* database) : db(database) { exec_sql(db, "BEGIN IMMEDIATE"); }
  ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
  void commit() { exec_sql(db, "COMMIT"); committed = true; }
};
void put_u32(std::vector<unsigned char>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 255));
}
uint32_t get_u32(const unsigned char* data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}
DATA_BLOB entropy_blob() {
  static constexpr char purpose[] = "Xenon/vault/v1/user-password";
  return {static_cast<DWORD>(sizeof(purpose) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(purpose))};
}
std::vector<unsigned char> encrypt(const std::string& username, const std::string& password) {
  SensitiveBytes plaintext;
  plaintext.value.reserve(12 + username.size() + password.size());
  put_u32(plaintext.value, 1);
  put_u32(plaintext.value, static_cast<uint32_t>(username.size()));
  put_u32(plaintext.value, static_cast<uint32_t>(password.size()));
  plaintext.value.insert(plaintext.value.end(), username.begin(), username.end());
  plaintext.value.insert(plaintext.value.end(), password.begin(), password.end());
  DATA_BLOB input{static_cast<DWORD>(plaintext.value.size()), plaintext.value.data()};
  auto entropy = entropy_blob();
  LocalBlob output;
  if (!CryptProtectData(&input, L"Xenon credential", &entropy, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output.value)) throw std::runtime_error("Credential encryption unavailable");
  return {output.value.pbData, output.value.pbData + output.value.cbData};
}
Secret decrypt(const unsigned char* data, int length) {
  if (!data || length <= 0) throw std::runtime_error("Credential unavailable");
  DATA_BLOB input{static_cast<DWORD>(length), const_cast<BYTE*>(data)};
  auto entropy = entropy_blob();
  LocalBlob output;
  if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value))
    throw std::runtime_error("Credential decryption unavailable");
  const auto* decoded = output.value.pbData;
  const auto size = output.value.cbData;
  if (size < 12 || get_u32(decoded) != 1) throw std::runtime_error("Credential format unavailable");
  const size_t username_size = get_u32(decoded + 4), password_size = get_u32(decoded + 8);
  if (username_size > kMaxField || password_size > kMaxField || username_size + password_size + 12 != size)
    throw std::runtime_error("Credential format unavailable");
  SensitiveText username{std::string(reinterpret_cast<const char*>(decoded + 12), username_size)};
  SensitiveText password{std::string(reinterpret_cast<const char*>(decoded + 12 + username_size), password_size)};
  return Secret(username.value, password.value);
}
// RFC 4180-style parser, accepting LF as well as CRLF and preserving quoted
// multiline values. No error includes input bytes. Limits prevent import DoS.
void parse_csv(const std::string& input, Rows& output) {
  size_t pos = input.starts_with("\xEF\xBB\xBF") ? 3 : 0;
  SensitiveText field;
  field.value.reserve(kMaxField);
  std::vector<std::string> row;
  struct RowWiper { std::vector<std::string>& row; ~RowWiper() { for (auto& s : row) wipe(s); } } row_wiper{row};
  bool quoted = false, after_quote = false, field_started = false;
  auto end_field = [&] {
    row.push_back(field.value); wipe(field.value);
    field_started = false; after_quote = false;
    if (row.size() > 64) throw std::runtime_error("CSV has too many columns");
  };
  auto end_row = [&] {
    end_field(); output.value.push_back(std::move(row)); row.clear();
    if (output.value.size() > 100001) throw std::runtime_error("CSV has too many rows");
  };
  for (; pos < input.size(); ++pos) {
    const char ch = input[pos];
    if (ch == '\0') throw std::runtime_error("CSV contains unsupported characters");
    if (quoted) {
      if (ch == '"') {
        if (pos + 1 < input.size() && input[pos + 1] == '"') { field.value.push_back('"'); ++pos; }
        else { quoted = false; after_quote = true; }
      } else field.value.push_back(ch);
    } else if (ch == ',') end_field();
    else if (ch == '\n' || ch == '\r') {
      if (ch == '\r' && pos + 1 < input.size() && input[pos + 1] == '\n') ++pos;
      end_row();
    } else if (after_quote) throw std::runtime_error("CSV contains data after a closing quote");
    else if (ch == '"') {
      if (field_started || !field.value.empty()) throw std::runtime_error("CSV contains an unexpected quote");
      quoted = true; field_started = true;
    } else { field.value.push_back(ch); field_started = true; }
    if (field.value.size() > kMaxField) throw std::runtime_error("CSV field is too large");
  }
  if (quoted) throw std::runtime_error("CSV has an unterminated quoted field");
  if (field_started || after_quote || !field.value.empty() || !row.empty()) end_row();
}
bool valid_secret(const std::string& value, bool allow_empty) {
  return (allow_empty || !value.empty()) && value.size() <= kMaxField &&
         value.find('\0') == std::string::npos && valid_utf8(value);
}
bool valid_label(const std::string& value) {
  return value.size() <= 160 && valid_utf8(value) && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
Json load_csv(const std::filesystem::path& path, SensitiveText& input, Rows& rows) {
  try {
    const auto size = std::filesystem::file_size(path);
    if (size > kMaxCsv) return failure("import_too_large", "The CSV import is limited to 16 MiB.");
    std::ifstream file(path, std::ios::binary);
    if (!file) return failure("import_unreadable", "The selected file could not be read.");
    input.value.resize(static_cast<size_t>(size));
    if (!file.read(input.value.data(), static_cast<std::streamsize>(size)) || file.peek() != std::char_traits<char>::eof())
      return failure("import_changed", "The selected file changed while being read.");
    if (!valid_utf8(input.value)) return failure("invalid_csv", "Use a UTF-8 browser password export.");
    parse_csv(input.value, rows);
  } catch (...) { return failure("invalid_csv", "The password CSV is malformed or unreadable. No credentials were imported."); }
  return success();
}
struct CsvColumns { size_t url = SIZE_MAX, username = SIZE_MAX, password = SIZE_MAX; };
Json csv_columns(const Rows& rows, CsvColumns& columns) {
  if (rows.value.empty()) return failure("invalid_csv", "The password CSV has no header.");
  const auto& header = rows.value.front();
  for (size_t i = 0; i < header.size(); ++i) {
    std::string key = header[i];
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t* column = key == "url" || key == "origin" || key == "hostname" ? &columns.url : key == "username" ? &columns.username : key == "password" ? &columns.password : nullptr;
    if (column) { if (*column != SIZE_MAX) return failure("invalid_csv", "The CSV contains duplicate credential columns."); *column = i; }
  }
  if (columns.url == SIZE_MAX || columns.username == SIZE_MAX || columns.password == SIZE_MAX)
    return failure("invalid_csv", "Expected URL, username and password columns from a browser export.");
  return success();
}
}

Secret::Secret(std::string username, std::string password) : username_(username), password_(password) {
  wipe(username); wipe(password);
}
Secret::~Secret() { clear(); }
Secret::Secret(Secret&& other) : username_(other.username_), password_(other.password_) { other.clear(); }
Secret& Secret::operator=(Secret&& other) {
  if (this != &other) { clear(); username_ = other.username_; password_ = other.password_; other.clear(); }
  return *this;
}
void Secret::clear() noexcept { wipe(username_); wipe(password_); }

struct Vault::Impl {
  struct Record {
    std::string id, label;
    SensitiveBytes payload;
  };
  struct Candidate {
    PendingCredential metadata;
    SensitiveBytes payload;
    std::optional<Record> expected;
    std::chrono::steady_clock::time_point created;
  };
  sqlite3* db{};
  mutable std::mutex mutex;
  bool is_locked{};
  PendingClock pending_clock;
  std::list<Candidate> pending;
  ~Impl() { if (db) sqlite3_close(db); }
  void expire_pending() {
    const auto now = pending_clock();
    pending.remove_if([&](const Candidate& candidate) {
      return now - candidate.created >= Vault::kPendingLoginLifetime;
    });
  }
  std::optional<Record> matching_record(const std::string& origin, const std::string& username) const {
    Statement query(db, "SELECT id,label,payload FROM accounts WHERE origin=?1"); query.text(1, origin);
    while (query.next()) {
      const auto* data = static_cast<const unsigned char*>(sqlite3_column_blob(query.stmt, 2));
      const auto size = sqlite3_column_bytes(query.stmt, 2);
      auto secret = decrypt(data, size);
      if (local_security::equal_secret(secret.username(), username))
        return Record{query.text(0), query.text(1), SensitiveBytes(std::vector<unsigned char>(data, data + size))};
    }
    return std::nullopt;
  }
  static bool same_record(const std::optional<Record>& left, const std::optional<Record>& right) {
    if (!left || !right) return !left && !right;
    return left->id == right->id && left->label == right->label && left->payload.value == right->payload.value;
  }
  std::optional<std::pair<std::string, Secret>> matching(const std::string& origin, const std::string& username) const {
    Statement query(db, "SELECT id, payload FROM accounts WHERE origin=?1"); query.text(1, origin);
    while (query.next()) {
      auto secret = decrypt(static_cast<const unsigned char*>(sqlite3_column_blob(query.stmt, 1)), sqlite3_column_bytes(query.stmt, 1));
      if (local_security::equal_secret(secret.username(), username)) return std::make_pair(query.text(0), std::move(secret));
    }
    return std::nullopt;
  }
  Json save_locked(const std::string& origin, const std::string& username, const std::string& password,
                   const std::string& label, const std::string& replacement) {
    const auto existing = matching(origin, username);
    if (existing && local_security::equal_secret(existing->second.password(), password) &&
        (replacement.empty() || (replacement == existing->first && label.empty())))
      return success({{"accountId", existing->first}, {"status", "unchanged"}});
    if (existing && existing->first != replacement)
      return failure("credential_conflict", "A different password is already saved for this account; confirm an update in Xenon.");
    std::string id = replacement;
    std::string previous_label;
    if (!replacement.empty()) {
      Statement check(db, "SELECT origin,label FROM accounts WHERE id=?1"); check.text(1, replacement);
      if (!check.next() || check.text(0) != origin) return failure("account_not_found", "The account is unavailable for this origin.");
      previous_label = check.text(1);
    } else id = local_security::random_hex(16);
    const auto encrypted = encrypt(username, password);
    const auto actual_label = !label.empty() ? label : !previous_label.empty() ? previous_label : "Account " + id.substr(0, 8);
    Statement write(db, "INSERT INTO accounts(id,origin,label,payload) VALUES(?1,?2,?3,?4) "
                        "ON CONFLICT(id) DO UPDATE SET label=excluded.label,payload=excluded.payload");
    write.text(1, id); write.text(2, origin); write.text(3, actual_label); write.bytes(4, encrypted); write.next();
    return success({{"accountId", id}, {"status", replacement.empty() ? "created" : "updated"}});
  }
};

Vault::Vault(const std::filesystem::path& database, PendingClock pending_clock) : impl_(std::make_unique<Impl>()) {
  impl_->pending_clock = pending_clock ? std::move(pending_clock) : [] { return std::chrono::steady_clock::now(); };
  if (database.empty() || !database.is_absolute() || database.parent_path().empty()) throw std::runtime_error("Credential storage needs an absolute directory");
  std::filesystem::create_directories(database.parent_path());
  local_security::restrict_path(database.parent_path());
  const auto filename = utf8_path(database);
  check_sql(sqlite3_open_v2(filename.c_str(), &impl_->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr));
  local_security::restrict_path(database);
  check_sql(sqlite3_busy_timeout(impl_->db, 3000));
  exec_sql(impl_->db, "PRAGMA journal_mode=DELETE; PRAGMA secure_delete=ON; PRAGMA temp_store=MEMORY; PRAGMA trusted_schema=OFF;"
                    "CREATE TABLE IF NOT EXISTS accounts(id TEXT PRIMARY KEY,origin TEXT NOT NULL,label TEXT NOT NULL,payload BLOB NOT NULL);"
                    "CREATE INDEX IF NOT EXISTS accounts_origin ON accounts(origin);");
}
Vault::~Vault() = default;

std::optional<std::string> Vault::normalize_https_origin(const std::string& url) {
  if (url.empty() || url.size() > 8192 || url.find_first_of("\r\n\t\\") != std::string::npos || url.find('\0') != std::string::npos) return std::nullopt;
  try {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;
    const auto authority_end = url.find_first_of("/?#", scheme_end + 3);
    const auto authority = url.substr(scheme_end + 3, authority_end == std::string::npos ? std::string::npos : authority_end - scheme_end - 3);
    const auto last_colon = authority.find_last_of(':');
    if (last_colon != std::string::npos && (authority.empty() || authority.front() != '[' || last_colon > authority.find(']'))) {
      const auto port_text = authority.substr(last_colon + 1);
      if (port_text.empty() || port_text.size() > 5 || !std::all_of(port_text.begin(), port_text.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;
      const auto port = std::stoul(port_text);
      if (port == 0 || port > 65535) return std::nullopt;
    }
    const auto wide = widen(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength ||
        !parts.dwHostNameLength || parts.nPort == 0) return std::nullopt;
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::string hostname;
    if (host.find(L':') != std::wstring::npos) {
      if (host.front() == L'[' && host.back() == L']') host = host.substr(1, host.size() - 2);
      IN6_ADDR address{}; wchar_t normalized[INET6_ADDRSTRLEN]{};
      if (InetPtonW(AF_INET6, host.c_str(), &address) != 1 || !InetNtopW(AF_INET6, &address, normalized, INET6_ADDRSTRLEN)) return std::nullopt;
      hostname = "[";
      for (const wchar_t ch : std::wstring(normalized)) hostname.push_back(static_cast<char>(ch));
      hostname += "]";
    } else {
      if (host.size() > 253 || host.front() == L'.' || host.back() == L'.') return std::nullopt;
      size_t label_size = 0;
      for (const auto ch : host) {
        if (ch == L'.') { if (!label_size || label_size > 63 || hostname.back() == '-') return std::nullopt; label_size = 0; }
        else {
          if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-') || (!label_size && ch == L'-')) return std::nullopt;
          ++label_size;
        }
        hostname.push_back(static_cast<char>(ch));
      }
      if (!label_size || label_size > 63 || hostname.back() == '-') return std::nullopt;
      // Reject legacy numeric IPv4 spellings which URL parsers may interpret
      // differently (127.1, octal or hex); dotted decimal is canonicalized.
      if (std::all_of(hostname.begin(), hostname.end(), [](unsigned char c) { return std::isdigit(c) || c == '.'; })) {
        IN_ADDR address{}; wchar_t normalized[INET_ADDRSTRLEN]{};
        if (InetPtonW(AF_INET, host.c_str(), &address) != 1 || !InetNtopW(AF_INET, &address, normalized, INET_ADDRSTRLEN)) return std::nullopt;
        std::string ipv4; for (wchar_t ch : std::wstring(normalized)) ipv4.push_back(static_cast<char>(ch));
        if (ipv4 != hostname) return std::nullopt;
      }
      const auto last_label = hostname.substr(hostname.find_last_of('.') == std::string::npos ? 0 : hostname.find_last_of('.') + 1);
      if (last_label.size() > 2 && last_label.starts_with("0x") && std::all_of(last_label.begin() + 2, last_label.end(), [](unsigned char c) { return std::isxdigit(c); })) return std::nullopt;
    }
    return "https://" + hostname + (parts.nPort == 443 ? std::string{} : ":" + std::to_string(parts.nPort));
  } catch (...) { return std::nullopt; }
}

Json Vault::list_accounts(std::optional<std::string> https_origin) const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before accessing saved accounts.");
  try {
    std::optional<std::string> origin;
    if (https_origin) { origin = normalize_https_origin(*https_origin); if (!origin) return failure("invalid_origin", "Saved accounts require an exact HTTPS origin."); }
    Statement query(impl_->db, origin ? "SELECT id,origin,label FROM accounts WHERE origin=?1 ORDER BY label,id" : "SELECT id,origin,label FROM accounts ORDER BY origin,label,id");
    if (origin) query.text(1, *origin);
    Json accounts = Json::array();
    while (query.next()) accounts.push_back({{"accountId", query.text(0)}, {"origin", query.text(1)}, {"label", query.text(2)}});
    return success({{"accounts", std::move(accounts)}});
  } catch (...) { return failure("vault_unavailable", "Saved account metadata is unavailable."); }
}
Json Vault::save(const std::string& origin_or_url, const std::string& username, const std::string& password,
                 const std::string& label, const std::string& replace_account_id) {
  const auto origin = normalize_https_origin(origin_or_url);
  if (!origin) return failure("invalid_origin", "Only valid HTTPS website logins can be saved.");
  if (!valid_secret(username, true) || !valid_secret(password, false) || !valid_label(label))
    return failure("invalid_credential", "The credential contains unsupported or oversized fields.");
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before saving an account.");
  try { return impl_->save_locked(*origin, username, password, label, replace_account_id); }
  catch (...) { return failure("vault_unavailable", "The credential could not be saved."); }
}
std::optional<Secret> Vault::get_secret(const std::string& account_id, const std::string& expected_https_origin) const {
  const auto origin = normalize_https_origin(expected_https_origin);
  if (!origin) return std::nullopt;
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return std::nullopt;
  try {
    Statement query(impl_->db, "SELECT payload FROM accounts WHERE id=?1 AND origin=?2"); query.text(1, account_id); query.text(2, *origin);
    if (!query.next()) return std::nullopt;
    return decrypt(static_cast<const unsigned char*>(sqlite3_column_blob(query.stmt, 0)), sqlite3_column_bytes(query.stmt, 0));
  } catch (...) { return std::nullopt; }
}
Json Vault::remove(const std::string& account_id) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before removing an account.");
  try {
    Statement remove(impl_->db, "DELETE FROM accounts WHERE id=?1"); remove.text(1, account_id); remove.next();
    return success({{"removed", sqlite3_changes(impl_->db) == 1}});
  } catch (...) { return failure("vault_unavailable", "The account could not be removed."); }
}
std::optional<PendingCredential> Vault::propose_login(const std::string& origin_or_url,
                                                     const std::string& username,
                                                     const std::string& password) {
  const auto origin = normalize_https_origin(origin_or_url);
  if (!origin || !valid_secret(username, true) || !valid_secret(password, false)) return std::nullopt;
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return std::nullopt;
  try {
    impl_->expire_pending();
    auto expected = impl_->matching_record(*origin, username);
    bool unchanged = false;
    if (expected) {
      auto previous = decrypt(expected->payload.value.data(), static_cast<int>(expected->payload.value.size()));
      unchanged = local_security::equal_secret(previous.password(), password);
    }
    // One prompt per exact origin and username. Repeated identical submissions
    // keep their existing expiry; a new value invalidates the older prompt.
    for (auto it = impl_->pending.begin(); it != impl_->pending.end();) {
      if (it->metadata.origin != *origin) { ++it; continue; }
      auto pending_secret = decrypt(it->payload.value.data(), static_cast<int>(it->payload.value.size()));
      if (!local_security::equal_secret(pending_secret.username(), username)) { ++it; continue; }
      if (!unchanged && local_security::equal_secret(pending_secret.password(), password) && Impl::same_record(it->expected, expected))
        return it->metadata;
      it = impl_->pending.erase(it);
    }
    if (unchanged) return std::nullopt;
    Impl::Candidate candidate{
      {local_security::random_hex(16), *origin, username.empty() ? "(empty username)" : "********", expected.has_value()},
      SensitiveBytes(encrypt(username, password)), std::move(expected), impl_->pending_clock()};
    const auto metadata = candidate.metadata;
    while (impl_->pending.size() >= kMaxPendingLogins) impl_->pending.pop_front();
    impl_->pending.push_back(std::move(candidate));
    return metadata;
  } catch (...) { return std::nullopt; }
}
std::vector<PendingCredential> Vault::pending_logins() const {
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return {};
  impl_->expire_pending();
  std::vector<PendingCredential> result;
  result.reserve(impl_->pending.size());
  for (const auto& candidate : impl_->pending) result.push_back(candidate.metadata);
  return result;
}
Json Vault::accept_login(const std::string& candidate_id, const std::string& label) {
  if (!valid_label(label)) return failure("invalid_credential", "Use a short account label without control characters.");
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before saving an account.");
  try {
    impl_->expire_pending();
    const auto found = std::find_if(impl_->pending.begin(), impl_->pending.end(), [&](const Impl::Candidate& candidate) {
      return candidate.metadata.candidate_id == candidate_id;
    });
    if (found == impl_->pending.end()) return failure("candidate_unavailable", "The save request expired or is no longer available.");
    auto candidate = std::move(*found);
    impl_->pending.erase(found);
    auto secret = decrypt(candidate.payload.value.data(), static_cast<int>(candidate.payload.value.size()));
    // The write lock also excludes another Vault instance changing the record
    // between revalidation and save. Ciphertext comparison detects ABA edits.
    Transaction transaction(impl_->db);
    const auto current = impl_->matching_record(candidate.metadata.origin, secret.username());
    if (!Impl::same_record(candidate.expected, current))
      return failure("candidate_stale", "The saved account changed after this request. Log in again before confirming an update.");
    const auto result = impl_->save_locked(candidate.metadata.origin, secret.username(), secret.password(), label,
                                          candidate.expected ? candidate.expected->id : std::string{});
    if (result.at("ok").get<bool>()) transaction.commit();
    return result;
  } catch (...) { return failure("vault_unavailable", "The credential could not be saved. Log in again to create a new save request."); }
}
void Vault::dismiss_login(const std::string& candidate_id) {
  std::lock_guard lock(impl_->mutex);
  impl_->expire_pending();
  impl_->pending.remove_if([&](const Impl::Candidate& candidate) { return candidate.metadata.candidate_id == candidate_id; });
}
Json Vault::preview_csv(const std::filesystem::path& path) const {
  SensitiveText input;
  Rows rows;
  const auto loaded = load_csv(path, input, rows);
  if (!loaded.at("ok").get<bool>()) return loaded;
  CsvColumns columns;
  const auto parsed = csv_columns(rows, columns);
  if (!parsed.at("ok").get<bool>()) return parsed;
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before previewing accounts.");
  try {
    size_t valid = 0, invalid = 0, conflicts = 0, duplicates = 0, total = 0;
    Json preview = Json::array();
    std::map<std::string, size_t> seen;
    for (size_t row_number = 1; row_number < rows.value.size(); ++row_number) {
      const auto& row = rows.value[row_number];
      if (row.size() == 1 && row[0].empty()) continue;
      ++total;
      if (row.size() != rows.value.front().size()) { ++invalid; continue; }
      const auto origin = normalize_https_origin(row[columns.url]);
      if (!origin || !valid_secret(row[columns.username], true) || !valid_secret(row[columns.password], false)) { ++invalid; continue; }
      ++valid;
      SensitiveText fingerprint_input{*origin + "\n" + row[columns.username]};
      const auto fingerprint = local_security::sha256(fingerprint_input.value);
      const auto prior = seen.find(fingerprint);
      const auto existing = impl_->matching(*origin, row[columns.username]);
      std::string status = "new";
      if (prior != seen.end()) {
        const auto& prior_password = rows.value[prior->second][columns.password];
        if (local_security::equal_secret(prior_password, row[columns.password])) { status = "duplicate"; ++duplicates; }
        else { status = "conflict"; ++conflicts; }
      } else if (existing) {
        if (local_security::equal_secret(existing->second.password(), row[columns.password])) { status = "existing"; ++duplicates; }
        else { status = "conflict"; ++conflicts; }
      }
      seen.emplace(fingerprint, row_number);
      if (preview.size() < 100) {
        Json item{{"row", row_number + 1}, {"origin", *origin}, {"usernameLabel", row[columns.username].empty() ? "(empty username)" : "********"}, {"status", status}};
        if (existing) item["accountId"] = existing->first;
        preview.push_back(std::move(item));
      }
    }
    return success({{"fileDigest", local_security::sha256(input.value)}, {"totalRows", total}, {"validRows", valid}, {"invalidRows", invalid},
                    {"conflicts", conflicts}, {"duplicates", duplicates}, {"preview", std::move(preview)}, {"previewTruncated", valid > 100}});
  } catch (...) { return failure("preview_failed", "The credential import preview is unavailable. Nothing was imported."); }
}
Json Vault::import_csv(const std::filesystem::path& path, bool replace_conflicts, const std::string& expected_file_digest) {
  SensitiveText input;
  Rows rows;
  const auto loaded = load_csv(path, input, rows);
  if (!loaded.at("ok").get<bool>()) return loaded;
  if (!expected_file_digest.empty()) {
    try { if (!local_security::equal_secret(local_security::sha256(input.value), expected_file_digest)) return failure("import_changed", "The CSV changed after preview. Preview the file again before importing."); }
    catch (...) { return failure("import_failed", "The import could not be verified."); }
  }
  CsvColumns columns;
  const auto parsed = csv_columns(rows, columns);
  if (!parsed.at("ok").get<bool>()) return parsed;
  const auto& header = rows.value.front();
  const auto url_column = columns.url, username_column = columns.username, password_column = columns.password;
  std::lock_guard lock(impl_->mutex);
  if (impl_->is_locked) return failure("vault_locked", "Unlock Xenon before importing accounts.");
  size_t created = 0, unchanged = 0, updated = 0, conflicts = 0, invalid = 0;
  Json conflict_accounts = Json::array();
  try {
    exec_sql(impl_->db, "BEGIN IMMEDIATE");
    for (size_t row_number = 1; row_number < rows.value.size(); ++row_number) {
      const auto& row = rows.value[row_number];
      if (row.size() == 1 && row[0].empty()) continue;
      if (row.size() != header.size()) { ++invalid; continue; }
      const auto origin = normalize_https_origin(row[url_column]);
      if (!origin || !valid_secret(row[username_column], true) || !valid_secret(row[password_column], false)) { ++invalid; continue; }
      const auto existing = impl_->matching(*origin, row[username_column]);
      std::string replacement;
      if (existing && !local_security::equal_secret(existing->second.password(), row[password_column])) {
        if (replace_conflicts) replacement = existing->first;
        else { ++conflicts; conflict_accounts.push_back({{"row", row_number + 1}, {"accountId", existing->first}, {"origin", *origin}}); continue; }
      }
      const auto result = impl_->save_locked(*origin, row[username_column], row[password_column], {}, replacement);
      if (!result.at("ok").get<bool>()) throw std::runtime_error("Import failed");
      const auto status = result.at("result").at("status").get<std::string>();
      if (status == "created") ++created; else if (status == "updated") ++updated; else ++unchanged;
    }
    exec_sql(impl_->db, "COMMIT");
  } catch (...) {
    sqlite3_exec(impl_->db, "ROLLBACK", nullptr, nullptr, nullptr);
    return failure("import_failed", "The import could not be completed. No credentials were imported.");
  }
  return success({{"created", created}, {"unchanged", unchanged}, {"updated", updated}, {"conflicts", conflicts},
                  {"invalid", invalid}, {"conflictAccounts", std::move(conflict_accounts)}, {"sourceFileRetained", true}});
}
void Vault::set_locked(bool locked) {
  std::lock_guard lock(impl_->mutex);
  impl_->is_locked = locked;
  if (locked) impl_->pending.clear();
}
bool Vault::locked() const { std::lock_guard lock(impl_->mutex); return impl_->is_locked; }
}
