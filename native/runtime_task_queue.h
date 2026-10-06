#pragma once
#include <deque>
#include <mutex>
#include <cstdint>
#include <vector>
namespace anyapi_runtime {
template<class Callback> class TaskQueue {
public:
    struct Job { uint64_t owner{}, ticket{}, epoch{}; Callback callback{}; void* user{}; };
private:
    std::mutex mutex_;
    std::deque<Job> pending_;
    uint64_t next_{1};
public:
    uint64_t post(uint64_t owner, uint64_t epoch, Callback callback, void* user) {
        if (!owner || !callback) return 0;
        std::lock_guard lock(mutex_);
        if (pending_.size() >= 128 || !next_) return 0;
        auto ticket = next_++; pending_.push_back({owner,ticket,epoch,callback,user}); return ticket;
    }
    bool cancel(uint64_t owner, uint64_t ticket) {
        std::lock_guard lock(mutex_);
        for (auto it=pending_.begin(); it!=pending_.end(); ++it)
            if (it->owner==owner && it->ticket==ticket) { pending_.erase(it); return true; }
        return false;
    }
    std::vector<Job> take() {
        std::lock_guard lock(mutex_); std::vector<Job> jobs;
        while (!pending_.empty() && jobs.size()<32) { jobs.push_back(pending_.front()); pending_.pop_front(); }
        return jobs;
    }
};
}
