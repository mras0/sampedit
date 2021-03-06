#ifndef SAMPEDIT_WIN32_BASE_H
#define SAMPEDIT_WIN32_BASE_H

#define NOMINMAX
#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <cassert>
#include <memory>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>

#include <base/event.h>

extern void fatal_error(const wchar_t* api, unsigned error = GetLastError());

constexpr COLORREF default_background_color = RGB(0x20, 0x20, 0x20);
constexpr COLORREF default_text_color       = RGB(0x3d, 0x74, 0xe2);

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
        return CreateSolidBrush(default_background_color); // GetSysColorBrush(COLOR_WINDOW);
    }

private:
    static ATOM class_atom_;
    HWND hwnd_;

    static HINSTANCE class_instance() {
        return GetModuleHandle(nullptr);
    }

    struct handler_base {
        static LRESULT invoke(Derived& d, UINT umsg, WPARAM wparam, LPARAM lparam, ...) {
            return d.wndproc(umsg, wparam, lparam);
        }
    };

    

#define MSG_HANDLERS(X) \
    X(WM_CREATE, (d.on_create() ? 0 : -1)) \
    X(WM_DESTROY, (d.on_destroy(), 0)) \
    X(WM_SIZE, (d.on_size(static_cast<UINT>(wparam), GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)), 0)) \
    X(WM_PAINT, (d.on_paint(), 0)) \
    X(WM_ERASEBKGND, (d.on_erase_background(reinterpret_cast<HDC>(wparam)) ? TRUE : FALSE)) \
    X(WM_NOTIFY, d.on_notify(*reinterpret_cast<const NMHDR*>(lparam))) \
    X(WM_CTLCOLORSTATIC, d.on_color_static(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam))) \
    X(WM_CTLCOLORLISTBOX, d.on_color_static(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam))) \
    X(WM_KEYDOWN, (d.on_key_down(static_cast<int>(wparam), static_cast<unsigned>(lparam)), 0)) \
    X(WM_COMMAND, (d.on_command(static_cast<int>(LOWORD(wparam)), reinterpret_cast<HWND>(lparam), static_cast<unsigned>(HIWORD(wparam))), 0)) \
    X(WM_TIMER, (d.on_timer(static_cast<uintptr_t>(wparam)), 0)) \
    X(WM_HSCROLL, (d.on_hscroll(static_cast<unsigned>(LOWORD(wparam)), static_cast<int>(static_cast<short>(HIWORD(wparam)))), 0)) \
    X(WM_MOUSEMOVE, (d.on_mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<unsigned>(wparam)), 0)) \
    X(WM_LBUTTONDOWN, (d.on_lbutton_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<unsigned>(wparam)), 0)) \
    X(WM_LBUTTONUP, (d.on_lbutton_up(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<unsigned>(wparam)), 0)) \
    X(WM_RBUTTONDOWN, (d.on_rbutton_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<unsigned>(wparam)), 0)) \
    X(WM_RBUTTONUP, (d.on_rbutton_up(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<unsigned>(wparam)), 0)) \

// Keep the above line blank to allow a backslash on the last line of the MSG_HANDLERS macro

#define IMPL_HANDLER(msg, expr) \
struct msg ## _handler : public handler_base { \
    using handler_base::invoke;                \
    template<typename D>                       \
    static LRESULT invoke(D& d, UINT, WPARAM wparam, LPARAM lparam, typename std::enable_if<true, decltype(expr)>::type*) { \
        (void)wparam; (void)lparam;             \
        return (LRESULT)(expr);                 \
    }                                           \
};
    MSG_HANDLERS(IMPL_HANDLER);
#undef IMPL_HANDLER

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
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
        LRESULT res;
        if (self) {
            auto& derived = static_cast<Derived&>(*self);
            switch (umsg) {
#define HANDLER_CASE(msg, expr) case msg: res = msg ## _handler::invoke(derived, umsg, wparam, lparam, nullptr); break;
                MSG_HANDLERS(HANDLER_CASE);
#undef HANDLER_CASE
            default:
                res = derived.wndproc(umsg, wparam, lparam);
            }
        } else {
            res = DefWindowProc(hwnd, umsg, wparam, lparam);
        }
        if (umsg == WM_NCDESTROY) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            delete self;
        }
        return res;
    }

#undef MSG_HANDLERS
};
template<typename Derived>
ATOM window_base<Derived>::class_atom_ = 0;

#endif