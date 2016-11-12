#include "pattern_edit.h"
#include <win32/gdi.h>

class pattern_edit_impl : public window_base<pattern_edit_impl> {
public:

private:
    font_ptr font_;

    friend window_base<pattern_edit_impl>;
    static const wchar_t* class_name() { return L"pattern_edit_impl"; }

    explicit pattern_edit_impl() : font_(create_default_tt_font(12)) {
    }


    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};


pattern_edit pattern_edit::create(HWND parent_wnd) {
    return pattern_edit{pattern_edit_impl::create(parent_wnd)->hwnd()};
}