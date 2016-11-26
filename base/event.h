#ifndef SAMPEDIT_BASE_EVENT_H
#define SAMPEDIT_BASE_EVENT_H

#include <functional>
#include <vector>
#include <cassert>

template<typename... ArgTypes>
using callback_function_type = std::function<void (ArgTypes...)>;

template<typename... ArgTypes>
class event {
public:
    using callback_type = callback_function_type<ArgTypes...>;

    void subscribe(const callback_type& cb) {
        assert(cb);
        subscribers_.push_back(cb);
    }

    void operator()(ArgTypes... args) {
        for (auto& s : subscribers_) {
            s(args...);
        }
    }

private:
    std::vector<callback_type> subscribers_;
};

#endif