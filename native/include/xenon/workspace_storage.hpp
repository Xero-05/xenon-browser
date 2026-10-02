#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xenon {
enum class WorkspaceCleanupStatus { removed, already_absent, deferred, refused, limit_reached };
const char* workspace_cleanup_status_name(WorkspaceCleanupStatus status) noexcept;
struct WorkspaceCleanupLimits {
  size_t max_workspaces = 256;
  size_t max_entries_per_workspace = 50000;
  size_t max_depth = 64;
  std::chrono::milliseconds max_elapsed{5000};
};
struct WorkspaceCleanupEntry {
  std::string workspace_id;
  WorkspaceCleanupStatus status = WorkspaceCleanupStatus::deferred;
  size_t files_removed = 0, directories_removed = 0;
  uint32_t system_error = 0;
};
struct WorkspaceCleanupResult {
  std::vector<WorkspaceCleanupEntry> entries;
  size_t removed = 0, already_absent = 0, deferred = 0, refused = 0, limited = 0, unprocessed = 0;
  size_t files_removed = 0, directories_removed = 0;
};

// Native startup only, after the application's CEF singleton is acquired and
// before any workspace request context is created. IDs must come from durable
// removal tombstones. This does not read state files, remove tombstones, or
// touch downloads, the vault, imported files, or any user-selected source.
// Failed/limited entries must remain tombstoned for explicit reporting/retry.
WorkspaceCleanupResult cleanup_workspace_storage(
    const std::filesystem::path& root, const std::vector<std::string>& workspace_ids,
    WorkspaceCleanupLimits limits = {});
}
