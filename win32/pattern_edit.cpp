#include "pattern_edit.h"
#include <win32/text_grid.h>

class pattern_edit_impl : public window_base<pattern_edit_impl> {
public:

    void update_grid(int centered_row) {
        text_grid_.centered_row(centered_row);
    }

private:
    friend window_base<pattern_edit_impl>;
    virtual_grid&   grid_;
    text_grid_view  text_grid_;

    static const wchar_t* class_name() { return L"pattern_edit_impl"; }

    explicit pattern_edit_impl(virtual_grid& grid) : grid_(grid) {
    }

    bool on_create() {
        text_grid_ = text_grid_view::create(hwnd(), grid_);
        return true;
    }

    void on_size(UINT, int cx, int cy) {
        SetWindowPos(text_grid_.hwnd(), nullptr, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void on_key_down(int vk, unsigned extra) {
        SendMessage(text_grid_.hwnd(), WM_KEYDOWN, vk, extra);
    }
};

pattern_edit pattern_edit::create(HWND parent_wnd, virtual_grid& grid) {
    return pattern_edit{pattern_edit_impl::create(parent_wnd, grid)->hwnd()};
}

void pattern_edit::update_grid(int centered_row) {
    pattern_edit_impl::from_hwnd(hwnd())->update_grid(centered_row);
}