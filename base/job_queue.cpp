#include "job_queue.h"

#include <queue>
#include <mutex>

class job_queue::impl {
public:
    using job_type = std::function<void(void)>;

    explicit impl() {
    }

    void post(const job_type& job) {
        std::lock_guard<std::mutex> lock{mutex_};
        jobs_.push(job);
    }

    void perform_all() {
        std::queue<job_type> jobs;
        {
            std::lock_guard<std::mutex> lock{mutex_};
            jobs = std::move(jobs_);
        }
        while (!jobs.empty()) {
            jobs.front()();
            jobs.pop();
        }
    }

private:
    std::mutex           mutex_;
    std::queue<job_type> jobs_;
};

job_queue::job_queue() : impl_(std::make_unique<impl>()) {
}

job_queue::~job_queue() = default;

void job_queue::post(const job_type& job) {
    impl_->post(job);
}

void job_queue::perform_all() {
    impl_->perform_all();
}
