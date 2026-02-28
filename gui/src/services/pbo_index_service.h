#pragma once

#include <armatools/pboindex.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// PboIndexService manages a background-loaded index of all PBO archive files
// found in the Arma 3 / workshop directories.
//
// A PBO (Packed BO) is the archive format used by Arma games.  The index lets
// other tabs (Asset Browser, Config Viewer, P3D Info, etc.) quickly resolve
// virtual paths like "a3\characters_f\backpack.p3d" to real files inside PBOs.
//
// How it works:
//   1. AppWindow calls set_db_path(cfg.a3db_path) after startup.
//   2. The service starts an async task to open the database.
//   3. When loading completes, all registered subscribers are notified.
//   4. Tabs receive the snapshot (containing the DB + index) and update themselves.
//
// Thread safety: all public methods are guarded by mutex_.
class PboIndexService : public std::enable_shared_from_this<PboIndexService> {
public:
    // A snapshot of the current index state delivered to subscribers.
    // All pointers are reference-counted (shared_ptr) and safe to keep alive.
    struct Snapshot {
        std::string db_path;  // Path to the database file that was opened.
        std::shared_ptr<armatools::pboindex::DB> db;      // Loaded PBO database (nullptr on error).
        std::shared_ptr<armatools::pboindex::Index> index; // Virtual path index (nullptr on error).
        std::string error;        // Non-empty if loading failed; contains the error message.
        std::size_t prefix_count = 0; // Number of search prefixes (directories) in the index.
    };

    // Callback type invoked on the GTK main thread when the index is ready.
    using Callback = std::function<void(const Snapshot&)>;

    PboIndexService() = default;

    // Set or change the database path. Triggers an asynchronous reload.
    void set_db_path(const std::string& path);

    // Force a reload of the current database path.
    void refresh();

    // Register a subscriber callback (identified by opaque owner pointer).
    // The callback is called every time the index is updated.
    // The owner pointer is only used as a key for unsubscribe(); it is never dereferenced.
    void subscribe(void* owner, Callback cb);

    // Remove the subscriber registered under owner (called in destructors to prevent
    // the callback from firing after the subscriber has been destroyed).
    void unsubscribe(void* owner);

    // Returns a copy of the current snapshot (thread-safe).
    Snapshot snapshot() const;

private:
    // Internal subscriber record.
    struct Subscriber {
        std::shared_ptr<std::atomic<bool>> alive; // Becomes false when subscriber unsubscribes.
        Callback cb;
    };

    mutable std::mutex mutex_;  // Protects all private fields below.
    std::string db_path_;       // Current database file path.
    std::shared_ptr<armatools::pboindex::DB> db_;       // Currently loaded database.
    std::shared_ptr<armatools::pboindex::Index> index_; // Currently active index.
    std::string error_;         // Last load error (empty = success).
    std::size_t prefix_count_ = 0;
    // Monotonically increasing counter used to discard results from stale async tasks.
    // If set_db_path() is called again before an async task finishes, the old task's
    // generation number will be lower than the current one and its result is ignored.
    std::uint64_t generation_ = 0;
    std::unordered_map<void*, Subscriber> subscribers_; // owner ptr -> subscriber record.

    // Starts an async background task to open the PBO database at `path`.
    void start_open_async(std::uint64_t generation, const std::string& path);

    // Called on the GTK main thread when the async open task finishes.
    // Checks that `generation` still matches the current one before storing results.
    void apply_open_result(std::uint64_t generation,
                           const std::string& path,
                           std::shared_ptr<armatools::pboindex::DB> db,
                           std::shared_ptr<armatools::pboindex::Index> index,
                           std::string error,
                           std::size_t prefix_count);

    // Sends the snapshot to every living subscriber on the GTK main thread.
    void emit_to_subscribers(const Snapshot& snap,
                             const std::vector<Subscriber>& subscribers);
};

