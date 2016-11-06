#ifndef SAMPEDIT_WIN32_BASE_H
#define SAMPEDIT_WIN32_BASE_H

#define NOMINMAX
#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <cassert>

extern void fatal_error(const wchar_t* api, unsigned error = GetLastError());

template<typename Derived>
class window_base {
public:
    HWND hwnd() const { return hwnd_; }

    template<typename... Args>
    static Derived* create(HWND parent, Args&&... args) {
        static bool class_registered = false;
        const auto hinst = Derived::class_instance();
        if (!class_atom_) {
            WNDCLASS wc;
            wc.style         = CS_VREDRAW | CS_HREDRAW;
            wc.lpfnWndProc   = window_base::s_wndproc;
            wc.cbClsExtra    = 0;
            wc.cbWndExtra    = 0;
            wc.hInstance     = hinst;
            wc.hIcon         = NULL;
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = Derived::create_background_brush();
            wc.lpszMenuName  = NULL;
            wc.lpszClassName = Derived::class_name();
            if ((class_atom_ = RegisterClass(&wc)) == 0) {
                fatal_error(L"RegisterClass");
            }
        }
        std::unique_ptr<Derived> wnd{new Derived(std::forward<Args>(args)...)};
        const HWND hwnd = CreateWindowEx(0, MAKEINTATOM(class_atom_), L"", parent ? WS_CHILD|WS_VISIBLE: WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, hinst, wnd.get());
        if (!hwnd) {
            fatal_error(L"CreateWindowEx");
        }
        assert(hwnd == wnd->hwnd());
        return wnd.release();
    }

    static Derived* from_hwnd(HWND hwnd) {
        assert(GetClassWord(hwnd, GCW_ATOM) == class_atom_);
        return reinterpret_cast<Derived *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    static HBRUSH create_background_brush() {
        return CreateSolidBrush(RGB(64, 64, 64)); // GetSysColorBrush(COLOR_WINDOW);
    }

private:
    static ATOM class_atom_;
    HWND hwnd_;

    static HINSTANCE class_instance() {
        return GetModuleHandle(nullptr);
    }

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam) {
        Derived* self;
        if (umsg == WM_NCCREATE) {
            LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lparam);
            self = reinterpret_cast<Derived *>(lpcs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(self));
        } else {
            self = from_hwnd(hwnd);
        }
        const LRESULT res = self ? self->wndproc(umsg, wparam, lparam) : DefWindowProc(hwnd, umsg, wparam, lparam);
        if (umsg == WM_NCDESTROY) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            delete self;
        }
        return res;
    }
};
template<typename Derived>
ATOM window_base<Derived>::class_atom_ = 0;

#endif