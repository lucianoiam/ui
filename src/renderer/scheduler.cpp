#include "scheduler.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace {
std::atomic<bool> g_dispatching{false};
std::mutex g_mutex;
std::vector<std::function<void()>> g_queue;
}

void scheduler_init() {}

void scheduler_request(const std::function<void()>& cb) {
    if (!cb) return;
    bool expected = false;
    if (g_dispatching.compare_exchange_strong(expected, true)) {
        // We won the right to run immediately; run callback then drain anything queued during execution.
        cb();
        while (true) {
            std::vector<std::function<void()>> local;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_queue.empty()) break;
                local.swap(g_queue);
            }
            for (auto &fn : local) if (fn) fn();
        }
        g_dispatching.store(false);
    } else {
        // Someone else executing; enqueue for later flush.
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.push_back(cb);
    }
}
