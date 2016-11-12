#ifndef SAMPEDIT_WIN32_MAIN_WINDOW_H
#define SAMPEDIT_WIN32_MAIN_WINDOW_H

#include <base/sample.h>
#include <base/note.h>
#include <base/virtual_grid.h>
#include <win32/base.h>

class main_window {
public:
    static main_window create(virtual_grid& grid);
    explicit main_window() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }
    int current_sample_index() const;
    void set_samples(const std::vector<sample>& s);

    void on_piano_key_pressed(const callback_function_type<piano_key>& cb);

private:
    explicit main_window(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif