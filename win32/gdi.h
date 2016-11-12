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

font_ptr create_default_font(int height);
font_ptr create_default_tt_font(int height);

#endif