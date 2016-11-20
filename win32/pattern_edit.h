#ifndef SAMPEDIT_WIN32_PATTERN_EDIT_H
#define SAMPEDIT_WIN32_PATTERN_EDIT_H

#include <win32/base.h>
#include "module.h"

class virtual_grid;

class pattern_edit {
public:
    static pattern_edit create(HWND parent_wnd, virtual_grid& grid);
    explicit pattern_edit() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }

    void set_module(const module& mod);
    void position_changed(const module_position& pos);

private:
    explicit pattern_edit(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif