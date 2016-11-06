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
        const HWND hwnd = CreateWindowEx(0, Derived::class_name(), L"", parent ? WS_CHILD|WS_VISIBLE: WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, hinst, wnd.get());
        if (!hwnd) {
            fatal_error(L"CreateWindowEx");
        }
        assert(hwnd == wnd->hwnd());
        return wnd.release();
    }

    static HBRUSH create_background_brush() {
        return CreateSolidBrush(RGB(64, 64, 64)); // GetSysColorBrush(COLOR_WINDOW);
    }

private:
    HWND hwnd_;

    static HINSTANCE class_instance() {
        return GetModuleHandle(nullptr);
    }

    static void register_class(HINSTANCE hinst) {
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
            const BOOL delete_ok = ::DeleteObject(obj);
            assert(delete_ok); (void)delete_ok;
        }
    }
};

template<typename T>
using gdi_obj_ptr = std::unique_ptr<T, delete_object_deleter>;

using pen_ptr    = gdi_obj_ptr<HPEN__>;
using brush_ptr  = gdi_obj_ptr<HBRUSH__>;
using bitmap_ptr = gdi_obj_ptr<HBITMAP__>;

struct dc_deleter {
    void operator()(HDC hdc) {
        if (hdc) {
            const BOOL delete_ok = ::DeleteDC(hdc);
            assert(delete_ok); (void)delete_ok;
        }
    }
};
using dc_ptr = std::unique_ptr<HDC__, dc_deleter>;

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

template<typename T>
gdi_obj_restorer select(const dc_ptr& dc, const gdi_obj_ptr<T>& obj) {
    return select(dc.get(), obj);
}

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

struct sample_range {
    float x0, x1;

    sample_range() : x0(0), x1(0) {}
    explicit sample_range(float start, float end) : x0(start), x1(end) {}

    float size() const { return x1 - x0; }
    bool valid() const { return size() > 0.0f; }

    float clamp(float x) {
        assert(valid());
        return std::max(x0, std::min(x, x1));
    }
};

class popup_menu {
public:
    explicit popup_menu() : menu_(CreatePopupMenu()) {
    }
    popup_menu(const popup_menu&) = delete;
    popup_menu& operator=(const popup_menu&) = delete;
    ~popup_menu() {
        DestroyMenu(menu_);
    }

    void insert(UINT_PTR id, const wchar_t* text) {
        InsertMenu(menu_, static_cast<UINT>(-1), MF_BYPOSITION|MF_STRING, id, text);
    }

    void track(int x, int y, HWND parent_wnd) {
        TrackPopupMenu(menu_, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, x, y, 0, parent_wnd, nullptr);
    }

private:
    HMENU menu_;
};


class sample_window : public window_base<sample_window> {
public:
    ~sample_window() = default;

    void set_sample(const sample& s) {
        sample_ = &s;
        undo_zoom();
    }

private:
    friend window_base<sample_window>;

    enum menu_ids {
        menu_id_zoom = 100,
        menu_id_undo_zoom,
    };

    explicit sample_window() : background_brush_(create_background_brush()) {
        menu_.insert(menu_id_zoom, L"Zoom");
        menu_.insert(menu_id_undo_zoom, L"Undo Zoom");
    }

    popup_menu    menu_;
    brush_ptr     background_brush_;

    POINT         size_;
    const sample* sample_ = nullptr;
    sample_range  zoom_;
    sample_range  selection_;

    enum class state {
        normal,
        selecting,
    } state_ = state::normal;

    static const wchar_t* class_name() { return L"sample_window"; }

    const int x_border = 10;
    const int y_border = 10;

    void undo_zoom() {
        zoom_ = sample_range{0.0f, static_cast<float>(sample_->length())};
    }

    int sample_pos_to_x(float pos) const {
        return static_cast<int>((pos - zoom_.x0) * static_cast<float>(size_.x - 2 * x_border) / zoom_.size());
    }

    float x_to_sample_pos(int x) const {
        return zoom_.x0 + static_cast<float>(x) * zoom_.size() / static_cast<float>(size_.x - 2 * x_border);
    }

    int sample_val_to_y(float val) const {
        return (size_.y / 2) + static_cast<int>(0.5 + val * (size_.y / 2 - y_border));
    }

    void paint(HDC hdc, const RECT& paint_rect) {
        FillRect(hdc, &paint_rect, background_brush_.get());

        if (!sample_ || !zoom_.valid()) return;

        pen_ptr pen{CreatePen(PS_SOLID, 1, RGB(255, 0, 0))};
        auto old_pen{select(hdc, pen)};
        RECT client_rect;
        GetClientRect(hwnd(), &client_rect);
        assert(client_rect.left == 0 && client_rect.top == 0);
        assert(client_rect.right == size_.x && client_rect.bottom == size_.y);

        bool first = true;
        for (int x = x_border; x < size_.x - x_border; ++x) {
            const int y = sample_val_to_y(sample_->get_linear(x_to_sample_pos(x-x_border)));
            if (first) {
                MoveToEx(hdc, x, y, nullptr);
                first = false;
            } else {
                LineTo(hdc, x, y);
            }
        }

        const RECT selection_rect = {
            x_border + sample_pos_to_x(selection_.x0), y_border, 
            x_border + sample_pos_to_x(selection_.x1), size_.y - y_border };
        InvertRect(hdc, &selection_rect);
    }

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT: {
                PAINTSTRUCT ps;
                if (BeginPaint(hwnd(), &ps)) {
                    if (!IsRectEmpty(&ps.rcPaint)) {
                        // Double buffer as per https://blogs.msdn.microsoft.com/oldnewthing/20060103-12/?p=32793
                        dc_ptr dc{CreateCompatibleDC(ps.hdc)};
                        if (dc) {
                            int x  = ps.rcPaint.left;
                            int y  = ps.rcPaint.top;
                            int cx = ps.rcPaint.right  - ps.rcPaint.left;
                            int cy = ps.rcPaint.bottom - ps.rcPaint.top;
                            bitmap_ptr bitmap{CreateCompatibleBitmap(ps.hdc, cx, cy)};
                            if (bitmap) {
                                auto old_bitmap = select(dc, bitmap);
                                SetWindowOrgEx(dc.get(), x, y, nullptr);
                                paint(dc.get(), ps.rcPaint);
                                BitBlt(ps.hdc, x, y, cx, cy, dc.get(), x, y, SRCCOPY);
                            }
                        }
                    }
                    EndPaint(hwnd(), &ps);
                    return 0;
                }
            }
            break;

        case WM_SIZE:
            size_ = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            break;

        case WM_LBUTTONDOWN:
            assert(state_ == state::normal);
            assert(sample_);
            state_ = state::selecting;
            selection_.x0 = selection_.x1 = zoom_.clamp(x_to_sample_pos(GET_X_LPARAM(lparam)));
            SetCapture(hwnd());
            break;

        case WM_LBUTTONUP:
            assert(state_ == state::selecting);
            assert(sample_);
            ReleaseCapture();
            state_ = state::normal;
            if (selection_.x1 < selection_.x0) {
                std::swap(selection_.x0, selection_.x1);
            }
            break;

        case WM_MOUSEMOVE:
            if (state_ == state::selecting) {
                assert(sample_);
                const auto new_end = zoom_.clamp(x_to_sample_pos(GET_X_LPARAM(lparam)));
                if (selection_.x1 != new_end) {
                    selection_.x1 = new_end;
                    InvalidateRect(hwnd(), nullptr, TRUE);
                }
            }
            break;

        case WM_RBUTTONUP: {
                RECT window_rect;
                GetWindowRect(hwnd(), &window_rect);
                menu_.track(window_rect.left + GET_X_LPARAM(lparam), window_rect.top + GET_Y_LPARAM(lparam), hwnd());
            }
            break;

        case WM_COMMAND:
            assert(state_ == state::normal);
            switch (LOWORD(wparam)) {
            case menu_id_zoom:
                if (selection_.valid()) {
                    zoom_ = selection_;
                    selection_ = sample_range{};
                    InvalidateRect(hwnd(), nullptr, TRUE);
                }
                break;
            case menu_id_undo_zoom:
                undo_zoom();
                InvalidateRect(hwnd(), nullptr, TRUE);
                break;
            }
            break;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

class main_window : public window_base<main_window> {
public:
    ~main_window() = default;

    void set_sample(const sample& s) {
        sample_wnd_->set_sample(s);
    }

private:
    friend window_base<main_window>;

    explicit main_window() = default;

    static const wchar_t* class_name() { return L"main_window"; }

    sample_window* sample_wnd_;

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_CREATE: {
                SetWindowText(hwnd(), L"SampEdit");
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
            SetWindowPos(sample_wnd_->hwnd(), nullptr, 0, 0, GET_X_LPARAM(lparam), /*GET_Y_LPARAM(lparam)*/400, SWP_NOZORDER | SWP_NOACTIVATE);
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
    sample samp{create_sample(44100/4)};
    auto& main_wnd = *main_window::create(nullptr);
    main_wnd.set_sample(samp);

    ShowWindow(main_wnd.hwnd(), SW_SHOW);
    UpdateWindow(main_wnd.hwnd());
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}