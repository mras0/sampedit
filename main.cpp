#define NOMINMAX
#define STRICT
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

void fatal_error(const wchar_t* api, unsigned error = GetLastError())
{
    wchar_t* msg;
    if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&msg),
        0,
        nullptr)) {
        wprintf(L"%s failed: %d (0x%X). %s\n", api, error, error, msg);
    } else {
        wprintf(L"%s failed: %d (0x%X)\n", api, error, error);
    }
    exit(error);
}

template<typename Derived>
class window_base {
public:
    HWND hwnd() const { return hwnd_; }

    template<typename... Args>
    static Derived* create(HWND parent, Args&&... args) {
        static bool class_registered = false;
        const auto hinst = Derived::class_instance();
        if (!class_registered) {
            Derived::register_class(hinst);
            class_registered = true;
        }
        std::unique_ptr<Derived> wnd{new Derived(std::forward<Args>(args)...)};
        const HWND hwnd = CreateWindowEx(0, Derived::class_name(), L"", WS_VISIBLE | (parent ? WS_CHILD : WS_OVERLAPPEDWINDOW), CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, hinst, wnd.get());
        if (!hwnd) {
            fatal_error(L"CreateWindowEx");
        }
        assert(hwnd == wnd->hwnd());
        return wnd.release();
    }
private:
    HWND hwnd_;

    static HINSTANCE class_instance() {
        return GetModuleHandle(nullptr);
    }

    static HBRUSH background_brush() {
        return reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    }

    static void register_class(HINSTANCE hinst) {
        WNDCLASS wc;
        wc.style         = 0;
        wc.lpfnWndProc   = window_base::s_wndproc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hinst;
        wc.hIcon         = NULL;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = Derived::background_brush();
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = Derived::class_name();
        if (!RegisterClass(&wc)) {
            fatal_error(L"RegisterClass");
        }
    }

    static LRESULT CALLBACK s_wndproc(HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam) {
        Derived* self;
        if (umsg == WM_NCCREATE) {
            LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lparam);
            self = reinterpret_cast<Derived *>(lpcs->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(self));
        } else {
            self = reinterpret_cast<Derived *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        const LRESULT res = self ? self->wndproc(umsg, wparam, lparam) : DefWindowProc(hwnd, umsg, wparam, lparam);
        if (umsg == WM_NCDESTROY) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            delete self;
        }
        return res;
    }
};

struct delete_object_deleter {
    void operator()(void* obj) {
        if (obj) {
            ::DeleteObject(obj);
        }
    }
};

template<typename T>
using gdi_obj_ptr = std::unique_ptr<T, delete_object_deleter>;

class gdi_obj_restorer {
public:
    explicit gdi_obj_restorer(HDC hdc, HGDIOBJ old) : hdc_(hdc), old_(old) {}
    gdi_obj_restorer(const gdi_obj_restorer&) = delete;
    gdi_obj_restorer& operator=(const gdi_obj_restorer&) = delete;
    gdi_obj_restorer(gdi_obj_restorer&& other) : hdc_(other.hdc_), old_(other.old_) {
        other.old_ = nullptr;
    }
    ~gdi_obj_restorer() {
        if (old_) {
            SelectObject(hdc_, old_);
        }
    }
private:
    HDC     hdc_;
    HGDIOBJ old_;
};

template<typename T>
gdi_obj_restorer select(HDC hdc, const gdi_obj_ptr<T>& obj) {
    return gdi_obj_restorer{hdc, ::SelectObject(hdc, obj.get())};
}

using pen_ptr = gdi_obj_ptr<HPEN__>;

class sample {
public:
    explicit sample(const std::vector<short>& data) : data_(data) {}

    int length() const { return static_cast<int>(data_.size()); }

    float get(int pos) const {
        return data_[pos] / 32768.0f;
    }

    float get_linear(float pos) const {
        const int ipos   = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(ipos);
        return (data_[ipos]*(1.0f-frac) + data_[std::min(ipos+1, length())]*frac) / 32768.0f;
    }

private:
    std::vector<short> data_;
};

const sample* g_sample;

class sample_window : public window_base<sample_window> {
public:
    ~sample_window() = default;

private:
    friend window_base<sample_window>;
    explicit sample_window() = default;

    static const wchar_t* class_name() { return L"sample_window"; }

    static HBRUSH background_brush() {
        return CreateSolidBrush(RGB(128, 128, 128));
    }

    void paint(HDC hdc) {
        if (!g_sample) return;
        const int sl = g_sample->length();
        if (sl < 1) return;

        pen_ptr pen{CreatePen(PS_SOLID, 1, RGB(255, 0, 0))};
        auto old_pen{select(hdc, pen)};
        RECT client_rect;
        GetClientRect(hwnd(), &client_rect);
        assert(client_rect.left == 0 && client_rect.top == 0);
        const int w = client_rect.right;
        const int h = client_rect.bottom;
        auto scale = [h](float val) { return (h/2) + static_cast<int>(val * h / 2); };

        MoveToEx(hdc, 0, scale(g_sample->get(0)), nullptr);
        for (int x = 1; x < w; ++x) {
            LineTo(hdc, x, scale(g_sample->get_linear(static_cast<float>(x) * static_cast<float>(sl) / static_cast<float>(w))));
        }
    }

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_PAINT: {
                PAINTSTRUCT ps;
                if (BeginPaint(hwnd(), &ps)) {
                    paint(ps.hdc);
                    EndPaint(hwnd(), &ps);
                    return 0;
                }
            }
            break;
        case WM_SIZE:
            InvalidateRect(hwnd(), nullptr, TRUE);
            break;
        
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

class main_window : public window_base<main_window> {
public:
    ~main_window() = default;
private:
    friend window_base<main_window>;

    explicit main_window() = default;

    static const wchar_t* class_name() { return L"main_window"; }

    sample_window* sample_wnd_;

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_CREATE: {
                sample_wnd_ = sample_window::create(hwnd());
            }
            break;

        case WM_NCDESTROY:
            PostQuitMessage(0);
            break;

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) SendMessage(hwnd(), WM_CLOSE, 0, 0);
            break;

        case WM_SIZE:
            SetWindowPos(sample_wnd_->hwnd(), nullptr, 0, 0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

std::vector<short> create_sample(int len, int rate=44100)
{
    std::vector<short> data(len);
    constexpr double pi = 3.14159265359;
    for (int i = 0; i < len; ++i) {
        data[i] = static_cast<short>(32767*std::cos(2*pi*440.0*i/rate));
    }
    return data;
}

int main()
{
    sample samp{create_sample(44100/16)};
    g_sample = &samp;
    main_window::create(nullptr);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}