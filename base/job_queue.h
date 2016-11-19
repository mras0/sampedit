#ifndef SAMPEDIT_BASE_JOB_QUEUE_H
#define SAMPEDIT_BASE_JOB_QUEUE_H

#include <functional>
#include <memory>

class job_queue {
public:
    using job_type = std::function<void(void)>;

    explicit job_queue();
    ~job_queue();

    void post(const job_type& job);
    void perform_all();

#ifndef NDEBUG
    void assert_in_queue_thread() const;
#else
    void assert_in_queue_thread() const {}
#endif

private:
    class impl;
    const std::unique_ptr<impl> impl_;
};

#endif