#include "pbo_index_service.h"

#include <glibmm/main.h>

#include <filesystem>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

void PboIndexService::set_db_path(const std::string& path) {
    std::vector<Subscriber> subscribers;
    Snapshot snap;
    std::uint64_t generation = 0;
    bool should_open = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (path == db_path_) return;

        db_path_ = path;
        db_.reset();
        index_.reset();
        error_.clear();
        prefix_count_ = 0;
        generation_++;
        generation = generation_;

        if (db_path_.empty()) {
            snap = Snapshot{db_path_, db_, index_, "", prefix_count_};
            subscribers.reserve(subscribers_.size());
            for (const auto& [_, sub] : subscribers_) subscribers.push_back(sub);
        } else if (!fs::exists(db_path_)) {
            error_ = "A3DB path does not exist: " + db_path_;
            snap = Snapshot{db_path_, db_, index_, error_, prefix_count_};
            subscribers.reserve(subscribers_.size());
            for (const auto& [_, sub] : subscribers_) subscribers.push_back(sub);
        } else {
            should_open = true;
        }
    }

    if (should_open) {
        start_open_async(generation, path);
    } else {
        emit_to_subscribers(snap, subscribers);
    }
}

void PboIndexService::refresh() {
    std::string path;
    std::uint64_t generation = 0;
    std::vector<Subscriber> subscribers;
    Snapshot snap;
    bool should_open = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        db_.reset();
        index_.reset();
        error_.clear();
        prefix_count_ = 0;
        generation_++;
        generation = generation_;
        path = db_path_;

        if (path.empty()) {
            snap = Snapshot{db_path_, db_, index_, "", prefix_count_};
            subscribers.reserve(subscribers_.size());
            for (const auto& [_, sub] : subscribers_) subscribers.push_back(sub);
        } else if (!fs::exists(path)) {
            error_ = "A3DB path does not exist: " + path;
            snap = Snapshot{db_path_, db_, index_, error_, prefix_count_};
            subscribers.reserve(subscribers_.size());
            for (const auto& [_, sub] : subscribers_) subscribers.push_back(sub);
        } else {
            should_open = true;
        }
    }

    if (should_open) {
        start_open_async(generation, path);
    } else {
        emit_to_subscribers(snap, subscribers);
    }
}

void PboIndexService::subscribe(void* owner, Callback cb) {
    if (!owner) return;

    Subscriber sub;
    sub.alive = std::make_shared<std::atomic<bool>>(true);
    sub.cb = std::move(cb);

    Snapshot snap;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscribers_.find(owner);
        if (it != subscribers_.end() && it->second.alive) {
            it->second.alive->store(false);
        }
        subscribers_[owner] = sub;
        snap = Snapshot{db_path_, db_, index_, error_, prefix_count_};
    }

    emit_to_subscribers(snap, {sub});
}

void PboIndexService::unsubscribe(void* owner) {
    if (!owner) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscribers_.find(owner);
    if (it == subscribers_.end()) return;
    it->second.alive->store(false);
    subscribers_.erase(it);
}

PboIndexService::Snapshot PboIndexService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Snapshot{db_path_, db_, index_, error_, prefix_count_};
}

void PboIndexService::start_open_async(std::uint64_t generation, const std::string& path) {
    auto self = shared_from_this();
    std::thread([self, generation, path]() {
        std::shared_ptr<armatools::pboindex::DB> db;
        std::shared_ptr<armatools::pboindex::Index> index;
        std::size_t prefix_count = 0;
        std::string error;

        try {
            db = std::make_shared<armatools::pboindex::DB>(armatools::pboindex::DB::open(path));
            index = std::make_shared<armatools::pboindex::Index>(db->index());
            prefix_count = static_cast<std::size_t>(index->size());
        } catch (const std::exception& e) {
            error = e.what();
            db.reset();
            index.reset();
            prefix_count = 0;
        } catch (...) {
            error = "Unknown error while opening A3DB";
            db.reset();
            index.reset();
            prefix_count = 0;
        }

        Glib::signal_idle().connect_once([self, generation, path, db, index, error, prefix_count]() {
            self->apply_open_result(generation, path, db, index, error, prefix_count);
        });
    }).detach();
}

void PboIndexService::apply_open_result(
    std::uint64_t generation,
    const std::string& path,
    std::shared_ptr<armatools::pboindex::DB> db,
    std::shared_ptr<armatools::pboindex::Index> index,
    std::string error,
    std::size_t prefix_count) {

    std::vector<Subscriber> subscribers;
    Snapshot snap;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_ || path != db_path_) return;

        db_ = std::move(db);
        index_ = std::move(index);
        error_ = std::move(error);
        prefix_count_ = prefix_count;

        snap = Snapshot{db_path_, db_, index_, error_, prefix_count_};
        subscribers.reserve(subscribers_.size());
        for (const auto& [_, sub] : subscribers_) subscribers.push_back(sub);
    }

    emit_to_subscribers(snap, subscribers);
}

void PboIndexService::emit_to_subscribers(
    const Snapshot& snap,
    const std::vector<Subscriber>& subscribers) {
    for (const auto& sub : subscribers) {
        auto alive = sub.alive;
        auto cb = sub.cb;
        Glib::signal_idle().connect_once([alive, cb, snap]() {
            if (!alive || !alive->load()) return;
            cb(snap);
        });
    }
}
