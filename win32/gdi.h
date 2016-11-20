#ifndef SAMPEDIT_WIN32_GDI_H
#define SAMPEDIT_WIN32_GDI_H

#include <win32/base.h>
#include <memory>

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
using font_ptr   = gdi_obj_ptr<HFONT__>;

struct dc_deleter {
    void operator()(HDC hdc) {
        if (hdc) {
            const BOOL delete_ok = ::DeleteDC(hdc);
            assert(delete_ok); (void)delete_ok;
        }
    }
};
using dc_ptr = std::unique_ptr<HDC__, dc_deleter>;

struct window_dc {
public:
    explicit window_dc(HWND wnd) : wnd_(wnd), dc_(::GetDC(wnd)) {
        assert(dc_);
    }
    window_dc(window_dc&& other) : wnd_(other.wnd_), dc_(other.dc_) {
        other.dc_ = nullptr;
    }
    window_dc& operator=(window_dc&& other) {
        assert(this != &other);
        dc_       = other.dc_;
        other.dc_ = nullptr;
        return *this;
    }
    ~window_dc() {
        if (dc_) {
            const BOOL release_ok = ::ReleaseDC(wnd_, dc_);
            assert(release_ok); (void)release_ok;
        }
    }

    HDC get() const {
        assert(dc_);
        return dc_;
    }

private:
    HWND    wnd_;
    HDC     dc_;
};


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

inline gdi_obj_restorer select(HDC hdc, HGDIOBJ obj) {
    return gdi_obj_restorer{hdc, ::SelectObject(hdc, obj)};
}

template<typename T>
gdi_obj_restorer select(HDC hdc, const gdi_obj_ptr<T>& obj) {
    return select(hdc, obj.get());
}

template<typename T>
gdi_obj_restorer select(const dc_ptr& dc, const gdi_obj_ptr<T>& obj) {
    return select(dc.get(), obj);
}

font_ptr create_default_font(int height);
font_ptr create_default_tt_font(int height);

inline void set_font(HWND window, const font_ptr& font) {
    assert(font);
    SendMessage(window, WM_SETFONT, reinterpret_cast<WPARAM>(font.get()), 0);
}

template<typename Derived>
struct double_buffered_paint {
    void on_paint() {
        auto& derived = static_cast<Derived&>(*this);
        PAINTSTRUCT ps;
        if (BeginPaint(derived.hwnd(), &ps)) {
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
                        derived.paint(dc.get(), ps.rcPaint);
                        BitBlt(ps.hdc, x, y, cx, cy, dc.get(), x, y, SRCCOPY);
                    }
                }
            }
            EndPaint(derived.hwnd(), &ps);
        }
    }
};

#endif