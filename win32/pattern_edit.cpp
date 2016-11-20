#include "pattern_edit.h"
#include <win32/text_grid.h>
#include <win32/gdi.h>
#include <algorithm>

class order_view : public window_base<order_view> {
public:
    SIZE min_size() const {
        return { 10 * font_width_, 2*y_spacing+font_height_ };
    }

    void set_module(const module& mod) {
        assert(!mod.order.empty());
        mod_ = &mod;
        selected(0);
    }

    void position_changed(const module_position& pos) {
        selected(pos.order);
    }

private:
    friend window_base<order_view>;
    static const wchar_t* class_name() { return L"order_view"; }
    static constexpr int font_height_ = 14;
    static constexpr int y_spacing = font_height_/2;
    font_ptr        font_;
    int             font_width_;
    const module*   mod_ = nullptr;
    int             selected_ = -1;

    explicit order_view() {
    }

    void selected(int sel) {
        assert(mod_);
        assert(sel >= 0 && sel < static_cast<int>(mod_->order.size()));
        if (sel != selected_) {
            selected_ = sel;
            InvalidateRect(hwnd(), nullptr, TRUE);
        }
    }

    bool on_create() {
        font_ = create_default_tt_font(font_height_);
        window_dc dc{hwnd()};
        auto old_font = select(dc.get(), font_);
        TEXTMETRIC tm;
        GetTextMetrics(dc.get(), &tm);
        font_width_ = tm.tmAveCharWidth;
        return true;
    }

    void on_destroy() {
    }

    int x_spacing() const {
        return font_width_*2;
    }

    int item_width() const {
        return font_width_ * 2 + x_spacing();
    }

    int position_to_x(int pos) const {
        assert(mod_);
        assert(pos >= 0 && pos < static_cast<int>(mod_->order.size()));
        return 2 + item_width() * pos;
    }

    RECT item_rect(int pos) const {
        const int x = position_to_x(pos); 
        return { x, 0, x + item_width(), min_size().cy };
    }

    void paint(HDC hdc, const RECT& /*paint_rect*/) {
        if (!mod_) return;

        SetBkColor(hdc, default_background_color);
        SetTextColor(hdc, default_text_color);
        auto old_font = select(hdc, font_);

        for (int i = 0; i < static_cast<int>(mod_->order.size()); ++i) {
            const auto text = std::to_wstring(mod_->order[i]);
            RECT text_rect = item_rect(i);
            DrawText(hdc, text.c_str(), static_cast<int>(text.length()), &text_rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        //
        // Selection
        //
        {
            pen_ptr dotted_pen{CreatePen(PS_DOT, 1, default_text_color)};
            auto old_pen = select(hdc, dotted_pen);
            auto old_brush = select(hdc, GetStockBrush(HOLLOW_BRUSH));

            auto r = item_rect(selected_);
            const int border = 2;
            InflateRect(&r, -border, -border);
            Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        }
    }

    void on_paint() {
        PAINTSTRUCT ps;
        if (BeginPaint(hwnd(), &ps)) {
            if (!IsRectEmpty(&ps.rcPaint)) {
                paint(ps.hdc, ps.rcPaint);
            }
            EndPaint(hwnd(), &ps);
        }
    }

};

class pattern_edit_impl : public window_base<pattern_edit_impl> {
public:
    void set_module(const module& mod) {
        order_view_->set_module(mod);
    }
    void position_changed(const module_position& pos) {
        text_grid_.centered_row(pos.row);
        order_view_->position_changed(pos);
    }

private:
    friend window_base<pattern_edit_impl>;
    virtual_grid&   grid_;
    text_grid_view  text_grid_;
    order_view*     order_view_;

    static const wchar_t* class_name() { return L"pattern_edit_impl"; }

    explicit pattern_edit_impl(virtual_grid& grid) : grid_(grid) {
    }

    bool on_create() {
        text_grid_  = text_grid_view::create(hwnd(), grid_);
        order_view_ = order_view::create(hwnd());
        return true;
    }

    void on_size(UINT, const int cx, const int cy) {
        const int order_view_min_y = order_view_->min_size().cy;
        const int order_view_y = std::max(order_view_min_y, cy - order_view_min_y);
        SetWindowPos(text_grid_.hwnd(), nullptr, 0, 0, cx, order_view_y, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(order_view_->hwnd(), nullptr, 0, order_view_y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void on_key_down(int vk, unsigned extra) {
        SendMessage(text_grid_.hwnd(), WM_KEYDOWN, vk, extra);
    }
};

pattern_edit pattern_edit::create(HWND parent_wnd, virtual_grid& grid) {
    return pattern_edit{pattern_edit_impl::create(parent_wnd, grid)->hwnd()};
}

void pattern_edit::set_module(const module& mod) {
    pattern_edit_impl::from_hwnd(hwnd())->set_module(mod);
}

void pattern_edit::position_changed(const module_position& pos) {
    pattern_edit_impl::from_hwnd(hwnd())->position_changed(pos);
}