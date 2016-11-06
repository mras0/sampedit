#ifndef SAMPEDIT_WIN32_SAMPLE_WINDOW_H
#define SAMPEDIT_WIN32_SAMPLE_WINDOW_H

#include <base/sample.h>
#include <win32/base.h>

class sample_window {
public:
    static sample_window create(HWND parent_wnd);
    explicit sample_window() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }
    void set_sample(const sample& s);

private:
    explicit sample_window(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif