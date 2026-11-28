#include "xenon/file_chooser.hpp"

#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace xenon;
namespace fc = xenon::file_chooser;

int main() {
  auto test = [](const char* name, const std::function<void()>& run) {
    run();
    std::cout << "PASS " << name << '\n';
  };

  test("mode_name maps supported and unsupported chooser modes", [] {
    assert(fc::mode_name(fc::ChooserMode::open) == "open");
    assert(fc::mode_name(fc::ChooserMode::open_multiple) == "openMultiple");
    assert(fc::mode_name(fc::ChooserMode::unsupported) == "unsupported");
  });

  test("is_safe_accept_filter accepts valid extensions and MIME types and rejects unsafe input", [] {
    for (const char* good : {
             ".pdf",
             ".docx",
             ".txt",
             ".c++",
             ".tar-gz",
             ".PNG",
             "image/*",
             "text/plain",
             "application/pdf",
             "application/vnd.ms-excel",
             "image/svg+xml",
             "application/x_custom-1.2",
         }) {
      assert(fc::is_safe_accept_filter(good));
    }

    const std::string too_long = "." + std::string(64, 'a');
    assert(too_long.size() == 65);
    assert(!fc::is_safe_accept_filter(too_long));
    assert(fc::is_safe_accept_filter("." + std::string(63, 'a')));

    for (const char* bad : {
             "",
             ".",
             "..",
             ".pdf.exe",
             ".pdf ",
             " .pdf",
             "pdf",
             "/",
             "image/",
             "/png",
             "image/png/extra",
             "image/png;charset=utf-8",
             "image/png\nignore",
             "C:\\Users\\alice\\secret.pdf",
             "\"PDF Documents\"",
             "text/plain,application/pdf",
             "*.pdf",
             "image/p*g?",
             ".pdf\x01",
             ".\xC3\xA9",
         }) {
      assert(!fc::is_safe_accept_filter(bad));
    }
  });

  test("sanitize_accept_filters deduplicates, drops invalid entries, and caps at 16", [] {
    std::vector<std::string> raw = {
        ".pdf",
        "bad filter with spaces",
        "image/*",
        ".pdf",
        "C:\\secret\\path.txt",
        "text/plain",
        "image/*",
    };
    for (int i = 0; i < 25; ++i) {
      raw.push_back(".ext" + std::to_string(i));
    }
    const auto cleaned = fc::sanitize_accept_filters(raw);
    assert(cleaned.size() == 16);
    assert(cleaned[0] == ".pdf");
    assert(cleaned[1] == "image/*");
    assert(cleaned[2] == "text/plain");
    assert(cleaned[3] == ".ext0");
    assert(cleaned[15] == ".ext12");
  });

  test("chooser JSON builders emit only allowlisted metadata without titles or paths", [] {
    const auto pending = fc::make_pending_chooser_json("cid-1", fc::ChooserMode::open_multiple, {".pdf", "image/*"}, true);
    assert(pending.size() == 4);
    assert(pending.at("chooserId") == "cid-1");
    assert(pending.at("mode") == "openMultiple");
    assert(pending.at("status") == "pending");
    assert(pending.at("acceptFilters") == Json::array({".pdf", "image/*"}));

    const auto status_view = fc::make_pending_chooser_json("cid-1", fc::ChooserMode::open, {".txt"}, false);
    assert(status_view.size() == 3);
    assert(!status_view.contains("status"));
    assert(status_view.at("mode") == "open");

    const auto rejected = fc::make_rejected_chooser_json("rejected", "unsupported_chooser_mode");
    assert(rejected == Json({{"status", "rejected"}, {"reason", "unsupported_chooser_mode"}}));

    const auto blocked = fc::make_rejected_chooser_json("blocked", "uploads_not_permitted");
    assert(blocked == Json({{"status", "blocked"}, {"reason", "uploads_not_permitted"}}));
  });

  test("valid_upload_params enforces three mutually exclusive upload modes", [] {
    // Direct mode (observationId + elementRef + fileId, or legacy broker fixture without fileId).
    assert(fc::valid_upload_params({{"observationId", "obs-1"}, {"elementRef", "ref-1"}, {"fileId", "f-1"}}));
    assert(fc::valid_upload_params({{"observationId", "obs-1"}, {"elementRef", "ref-1"}}));
    // Fulfill intercepted chooser.
    assert(fc::valid_upload_params({{"chooserId", "c-1"}, {"fileId", "f-1"}}));
    // Cancel intercepted chooser.
    assert(fc::valid_upload_params({{"chooserId", "c-1"}, {"cancel", true}}));

    // Invalid combinations must fail closed.
    assert(!fc::valid_upload_params(Json::array()));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"cancel", false}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"cancel", "true"}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"fileId", "f-1"}, {"cancel", true}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"fileId", "f-1"}, {"observationId", "obs-1"}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"fileId", "f-1"}, {"elementRef", "ref-1"}}));
    assert(!fc::valid_upload_params({{"chooserId", "c-1"}, {"cancel", true}, {"observationId", "obs-1"}}));
    assert(!fc::valid_upload_params({{"observationId", "obs-1"}, {"elementRef", "ref-1"}, {"cancel", true}}));
  });

  std::cout << "All file_chooser_tests passed\n";
  return 0;
}
