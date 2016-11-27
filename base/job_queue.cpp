#include "job_queue.h"

#include <queue>
#include <mutex>
#include <thread>
#include <cassert>

class job_queue::impl {
public:
    using job_type = std::function<void(void)>;

    explicit impl() {
    }

    void post(const job_type& job) {
        std::lock_guard<std::mutex> lock{mutex_};
        jobs_.push(job);
    }

    void dispatch(const job_type& job) {
        bool sync = false;
        std::mutex sync_mutex;
        std::condition_variable sync_cv;

        {
            std::lock_guard<std::mutex> job_lock{mutex_};
            assert(std::this_thread::get_id() != thread_id_);
            jobs_.push(job);
            jobs_.push([&] {
                {
                    std::lock_guard<std::mutex> sync_lock{sync_mutex};
                    sync = true;
                }
                sync_cv.notify_one();
            });
        }
        std::unique_lock<std::mutex> sync_lock{sync_mutex};
        sync_cv.wait(sync_lock, [&] { return sync; });
    }

    void perform_all() {
        std::queue<job_type> jobs;
        {
            std::lock_guard<std::mutex> lock{mutex_};

            if (thread_id_ == std::thread::id()) {
                thread_id_ = std::this_thread::get_id();
            } else {
                assert(thread_id_ == std::this_thread::get_id());
            }

            jobs = std::move(jobs_);
        }
        while (!jobs.empty()) {
            jobs.front()();
            jobs.pop();
        }
    }

#ifndef NDEBUG
    void assert_in_queue_thread() const {
        std::lock_guard<std::mutex> lock{mutex_};
        assert(thread_id_ == std::thread::id() || thread_id_ == std::this_thread::get_id());
    }
#endif

private:
    mutable std::mutex   mutex_;
    std::queue<job_type> jobs_;
    std::thread::id      thread_id_;
};

job_queue::job_queue() : impl_(std::make_unique<impl>()) {
}

job_queue::~job_queue() = default;

void job_queue::post(const job_type& job) {
    impl_->post(job);
}

void job_queue::dispatch(const job_type& job) {
    impl_->dispatch(job);
}

void job_queue::perform_all() {
    impl_->perform_all();
}

#ifndef NDEBUG
void job_queue::assert_in_queue_thread() const {
    impl_->assert_in_queue_thread();
}
#endif
