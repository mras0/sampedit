#ifndef SAMPEDIT_WIN32_MAIN_WINDOW_H
#define SAMPEDIT_WIN32_MAIN_WINDOW_H

#include <base/sample.h>
#include <win32/base.h>

class main_window {
public:
    static main_window create();
    explicit main_window() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }
    void set_samples(const std::vector<sample>& s);

private:
    explicit main_window(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif