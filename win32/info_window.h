#ifndef SAMPEDIT_WIN32_INFO_WINDOW_H
#define SAMPEDIT_WIN32_INFO_WINDOW_H

#include <win32/base.h>
#include "module.h"

class info_window {
public:
    static info_window create(HWND parent_wnd);
    explicit info_window() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }

    void set_module(const module& mod);
    void position_changed(const module_position& pos);
private:
    explicit info_window(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif