#include "xenon/extension_store.hpp"
#include "xenon/local_security.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace xenon;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void write(const fs::path& path, const std::string& text) { fs::create_directories(path.parent_path()); std::ofstream(path, std::ios::binary) << text; }
std::string refused(ExtensionStore& store, const fs::path& folder) {
  try { store.install_unpacked(folder); } catch (const std::runtime_error& error) { return error.what(); }
  return {};
}
}
int main() {
  try {
    // Synthetic folders only; no real browser profile or extension is read.
    const auto base = fs::absolute(fs::path("extension-store-tests") / local_security::random_hex(8));
    const auto data = base / "data", sources = base / "sources";
    fs::create_directories(data);
    require(ExtensionStore::generate_id("test") == "jpignaibiiemhngfjkcpokkamffknabf", "Chromium's ID alphabet maps SHA-256 hex to a-p");
    write(sources / "basic" / "manifest.json", R"({
      // Chromium accepts comments in manifests.
      "manifest_version": 3, "name": "__MSG_appName__", "version": "1.2.3", "default_locale": "en",
      "options_ui": {"page": "./options.html"}, "action": {"default_popup": "popup/index.html"}})");
    write(sources / "basic" / "_locales" / "en" / "messages.json", R"({"APPNAME":{"message":"Synthetic Reader"}})");
    write(sources / "basic" / "popup" / "index.html", "<!doctype html>");
    write(sources / "legacy" / "manifest.json", R"({"manifest_version":2,"name":"Old","version":"1"})");
    write(sources / "unsafe" / "manifest.json", R"({"manifest_version":3,"name":"Unsafe","version":"1","options_page":"../../escape.html"})");
    write(sources / "nameless" / "manifest.json", R"({"manifest_version":3,"version":"1"})");
    ExtensionStore store(data);
    require(store.list().empty() && store.startup_paths().empty(), "A new profile has no extensions");
    require(refused(store, sources / "legacy").find("Manifest V2") != std::string::npos, "Manifest V2 is refused with an explanation");
    require(!refused(store, sources / "nameless").empty(), "A manifest needs a name and version");
    require(!refused(store, sources / "missing").empty(), "A missing folder is refused");
    require(!refused(store, data).empty(), "Xenon's own data directory cannot be installed as an extension");
    const auto record = store.install_unpacked(sources / "basic");
    require(record.name == "Synthetic Reader" && record.version == "1.2.3" && record.enabled, "Localized name and version are recorded");
    require(record.options_page == "options.html" && record.popup_page == "popup/index.html", "Extension-relative pages are normalized");
    require(record.id == ExtensionStore::unpacked_id(store.directory(record)) && record.id.size() == 32, "Unpacked ID follows Chromium's path rule");
    require(fs::is_regular_file(store.directory(record) / "popup" / "index.html") && store.directory(record).parent_path() == fs::absolute(data / "extensions").lexically_normal(), "The folder is copied into managed storage");
    write(sources / "basic" / "manifest.json", R"({"manifest_version":3,"name":"Changed","version":"9"})");
    require(ExtensionStore(data).find(record.id)->name == "Synthetic Reader", "Later source changes do not affect the managed copy");
    const auto unsafe = store.install_unpacked(sources / "unsafe");
    require(unsafe.options_page.empty(), "Pages outside the extension are never opened");
    require(store.active_ids().empty(), "Nothing is active before the browser starts");
    {
      ExtensionStore restarted(data);
      const auto paths = restarted.startup_paths();
      require(paths.size() == 2 && restarted.active(record.id), "Enabled extensions load at the next start");
      require(restarted.set_enabled(record.id, false) && !restarted.active(record.id), "Disabling stops native page access immediately");
      require(restarted.remove(unsafe.id) && !restarted.find(unsafe.id), "Removal unregisters the extension");
      require(fs::exists(restarted.directory(unsafe)), "A loaded extension's files wait until the next start");
    }
    {
      ExtensionStore restarted(data);
      require(!fs::exists(restarted.directory(unsafe)), "Pending deletion finishes at the next start");
      require(restarted.startup_paths().empty() && !restarted.find(record.id)->enabled, "Disabled state persists");
      require(restarted.set_enabled(record.id, true), "Re-enable");
      require(!restarted.active(record.id), "Re-enabled extensions wait for restart");
      require(restarted.remove(record.id) && !fs::exists(restarted.directory(record)), "An extension not loaded this session is deleted at once");
    }
#ifndef _WIN32
    write(sources / "linked" / "manifest.json", R"({"manifest_version":3,"name":"Linked","version":"1"})");
    fs::create_directory_symlink(sources / "basic", sources / "linked" / "outside");
    require(refused(store, sources / "linked").find("links") != std::string::npos, "Links inside an extension folder are refused");
    require(!fs::exists(data / "extensions") || std::none_of(fs::directory_iterator(data / "extensions"), fs::directory_iterator{}, [](const auto& entry) { return entry.path().extension() == ".partial"; }), "A refused copy leaves nothing behind");
#endif
    require(fs::canonical(base).parent_path() == fs::absolute("extension-store-tests"), "Cleanup stays inside the generated fixture directory");
    fs::remove_all(base);
    std::cout << "Extension store tests passed: Manifest V3 validation, Chromium IDs, managed copies, link refusal, restart activation and deferred deletion\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
