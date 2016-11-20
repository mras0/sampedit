#ifndef SAMPEDIT_WIN32_MAIN_WINDOW_H
#define SAMPEDIT_WIN32_MAIN_WINDOW_H

#include <base/sample.h>
#include <base/note.h>
#include <base/virtual_grid.h>
#include <win32/base.h>
#include "module.h"

class main_window {
public:
    static main_window create(virtual_grid& grid);
    explicit main_window() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }
    int current_sample_index() const;
    void set_module(const module& mod);

    void on_exiting(const callback_function_type<>& cb);
    void on_piano_key_pressed(const callback_function_type<piano_key>& cb);
    void on_start_stop(const callback_function_type<>& cb);
    void on_order_selected(const callback_function_type<int>& cb);
    void position_changed(const module_position& pos);

private:
    explicit main_window(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif