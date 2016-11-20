#include "pattern_edit.h"
#include <win32/text_grid.h>
#include <win32/gdi.h>
#include <algorithm>

class order_view : public window_base<order_view> {
public:
    SIZE min_size() const {
        return { 10 * font_width_, item_height_ + scroll_bar_height_ };
    }

    void set_module(const module& mod) {
        assert(!mod.order.empty());
        mod_ = &mod;
        selected(0);
    }

    void position_changed(const module_position& pos) {
        selected(pos.order);
    }

    void on_order_selected(const callback_function_type<int>& cb) {
        on_order_selected_.subscribe(cb);
    }

private:
    friend window_base<order_view>;
    static const wchar_t* class_name() { return L"order_view"; }
    static constexpr int font_height_ = 14;
    static constexpr int y_spacing = font_height_/2;
    static constexpr int item_height_ = 2*y_spacing+font_height_;
    static constexpr int scroll_bar_height_ = 10;
    font_ptr        font_;
    int             font_width_;
    const module*   mod_ = nullptr;
    int             selected_ = -1;
    HWND            scrollbar_;
    event<int>      on_order_selected_;

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
        SetWindowLong(hwnd(), GWL_STYLE, GetWindowLong(hwnd(), GWL_STYLE) | WS_CLIPCHILDREN);

        font_ = create_default_tt_font(font_height_);
        window_dc dc{hwnd()};
        auto old_font = select(dc.get(), font_);
        TEXTMETRIC tm;
        GetTextMetrics(dc.get(), &tm);
        font_width_ = tm.tmAveCharWidth;

        // Show as needed by scroll_to
        scrollbar_ = CreateWindow(L"SCROLLBAR", L"", WS_CHILD|SBS_HORZ, 0, 0, 100, scroll_bar_height_, hwnd(), nullptr, nullptr, nullptr);
        if (!scrollbar_) {
            fatal_error(L"CreateWindow(SCROLLBAR)");
        }
        return true;
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

    int x_to_position(int x) const {
        const int pos = (x-2) / item_width();
        return mod_ && pos >= 0 && pos < static_cast<int>(mod_->order.size()) ? pos : -1;
    }

    RECT item_rect(int pos) const {
        const int x = position_to_x(pos); 
        return { x, 0, x + item_width(), item_height_ };
    }

    int items_per_page_ = 0;
    int scroll_origin_ = 0;

    void scroll_to(int pos) {
        const int num_items = mod_ ? static_cast<int>(mod_->order.size()) : 1;
    
        pos = std::max(0, std::min<int>(pos, num_items - items_per_page_));
        ScrollWindowEx(hwnd(), (scroll_origin_ - pos) * item_width(), 0,  NULL, NULL, NULL, NULL, SW_ERASE | SW_INVALIDATE);
        scroll_origin_ = pos;

        if (num_items <= items_per_page_) {
            assert(scroll_origin_ == 0);
            ShowWindow(scrollbar_, SW_HIDE);
            return;
        }

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
        si.nPage = items_per_page_;
        si.nMin = 0;
        si.nMax = num_items - 1;
        si.nPos = scroll_origin_;
        SetScrollInfo(scrollbar_, SB_CTL, &si, TRUE);
        ShowWindow(scrollbar_, SW_SHOW);
    }

    void scroll_delta(int delta) {
        scroll_to(scroll_origin_ + delta);
    }

    void on_size(UINT, const int cx, const int /*cy*/) {
        if (!cx) return;
        SetWindowPos(scrollbar_, nullptr, 0, item_height_, cx, scroll_bar_height_, SWP_NOZORDER | SWP_NOACTIVATE);
        items_per_page_ = std::max(1, cx / item_width());
        scroll_delta(0);
    }

    void on_hscroll(unsigned code, int pos) {
        switch (code) {
        case SB_LINEUP:         scroll_delta(-1); break;
        case SB_LINEDOWN:       scroll_delta(+1); break;
        case SB_PAGEUP:         scroll_delta(-items_per_page_); break;
        case SB_PAGEDOWN:       scroll_delta(+items_per_page_); break;
        case SB_THUMBPOSITION:  scroll_to(pos); break;
        case SB_THUMBTRACK:     scroll_to(pos); break;
        case SB_TOP:            scroll_to(0); break;
        case SB_BOTTOM:         scroll_to(MAXLONG); break;
        }
    }

    void on_lbutton_down(int x, int y, unsigned) {
        if (y < y_spacing || y > item_height_ - y_spacing) return;

        const int pos = x_to_position(x);
        if (pos >= 0 && pos + scroll_origin_ < static_cast<int>(mod_->order.size())) {
            on_order_selected_(pos + scroll_origin_);
        }
    }

    void paint(HDC hdc, const RECT& paint_rect) {
        if (!mod_) return;

        SetBkColor(hdc, default_background_color);
        SetTextColor(hdc, default_text_color);
        auto old_font = select(hdc, font_);

        const int iwidth = item_width();
        int imin = std::max<int>(paint_rect.left / iwidth, 0);
        int imax = std::min<int>((paint_rect.right + iwidth - 1) / iwidth, static_cast<int>(mod_->order.size()));

        for (int i = imin; i < imax; ++i) {
            const auto text = std::to_wstring(mod_->order[i]);
            RECT text_rect = item_rect(i);
            DrawText(hdc, text.c_str(), static_cast<int>(text.length()), &text_rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }

        //
        // Selection
        //
        if (selected_ >= imin && selected_ <= imax) {
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
                POINT ptOrgPrev;
                const int xscroll = scroll_origin_ * item_width();
                OffsetRect(&ps.rcPaint, xscroll, 0);
                GetWindowOrgEx(ps.hdc, &ptOrgPrev);
                SetWindowOrgEx(ps.hdc, ptOrgPrev.x + xscroll, ptOrgPrev.y, NULL);
                paint(ps.hdc, ps.rcPaint);
                SetWindowOrgEx(ps.hdc, ptOrgPrev.x, ptOrgPrev.y, NULL);
                
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
        InvalidateRect(text_grid_.hwnd(), nullptr, TRUE); // Force text grid to repaint
        order_view_->position_changed(pos);
    }

    void on_order_selected(const callback_function_type<int>& cb) {
        order_view_->on_order_selected(cb);
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

void pattern_edit::on_order_selected(const callback_function_type<int>& cb) {
    pattern_edit_impl::from_hwnd(hwnd())->on_order_selected(cb);
}