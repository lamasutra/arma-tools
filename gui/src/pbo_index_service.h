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

class PboIndexService : public std::enable_shared_from_this<PboIndexService> {
public:
    struct Snapshot {
        std::string db_path;
        std::shared_ptr<armatools::pboindex::DB> db;
        std::shared_ptr<armatools::pboindex::Index> index;
        std::string error;
        std::size_t prefix_count = 0;
    };

    using Callback = std::function<void(const Snapshot&)>;

    PboIndexService() = default;

    void set_db_path(const std::string& path);
    void refresh();

    void subscribe(void* owner, Callback cb);
    void unsubscribe(void* owner);

    Snapshot snapshot() const;

private:
    struct Subscriber {
        std::shared_ptr<std::atomic<bool>> alive;
        Callback cb;
    };

    mutable std::mutex mutex_;
    std::string db_path_;
    std::shared_ptr<armatools::pboindex::DB> db_;
    std::shared_ptr<armatools::pboindex::Index> index_;
    std::string error_;
    std::size_t prefix_count_ = 0;
    std::uint64_t generation_ = 0;
    std::unordered_map<void*, Subscriber> subscribers_;

    void start_open_async(std::uint64_t generation, const std::string& path);
    void apply_open_result(std::uint64_t generation,
                           const std::string& path,
                           std::shared_ptr<armatools::pboindex::DB> db,
                           std::shared_ptr<armatools::pboindex::Index> index,
                           std::string error,
                           std::size_t prefix_count);

    void emit_to_subscribers(const Snapshot& snap,
                             const std::vector<Subscriber>& subscribers);
};
