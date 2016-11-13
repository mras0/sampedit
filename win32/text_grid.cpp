#include "text_grid.h"
#include <win32/gdi.h>
#include <algorithm>

class text_grid_view_impl : public window_base<text_grid_view_impl>, public double_buffered_paint<text_grid_view_impl> {
public:
    void centered_row(int row) {
        if (row != centered_row_) {
            centered_row_ = row;
            InvalidateRect(hwnd(), nullptr, TRUE);
        }
    }

private:
    virtual_grid&   grid_;
    font_ptr        font_;
    int             centered_row_ = 0;

    friend window_base<text_grid_view_impl>;
    friend double_buffered_paint<text_grid_view_impl>;

    static const wchar_t* class_name() { return L"text_grid_view_impl"; }

    explicit text_grid_view_impl(virtual_grid& grid) : grid_(grid), font_(create_default_tt_font(14)) {
    }

    bool on_erase_background(HDC) {
        return true;
    }

    void on_key_down(int vk, unsigned /*extra*/) {
        constexpr int rows_per_page = 16;
        if (vk == VK_UP) {
            const int rows = grid_.rows();
            centered_row((centered_row_ - 1 + rows) % rows);
        } else if (vk == VK_DOWN) {
            centered_row((centered_row_ + 1) % grid_.rows());
        } else  if (vk == VK_PRIOR) {
            centered_row(std::max(centered_row_ - rows_per_page, 0));
        } else if (vk == VK_NEXT) {
            centered_row(std::min(centered_row_ + rows_per_page, grid_.rows() - 1));
        }
    }

    void paint(HDC hdc, const RECT& paint_rect_) {
        SetDCBrushColor(hdc, default_background_color);
        FillRect(hdc, &paint_rect_, GetStockBrush(DC_BRUSH));

        SetTextColor(hdc, default_text_color);
        SetBkColor(hdc, default_background_color);

        auto old_font = select(hdc, font_);

        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        const SIZE font_size{tm.tmMaxCharWidth, tm.tmHeight};

        const int x_spacing = font_size.cy*2;
        const int y_spacing = 1;

        const int line_height = font_size.cy + 2 * y_spacing;

        RECT client_rect;
        GetClientRect(hwnd(), &client_rect);
        const int mid_y = client_rect.bottom/2;

        //
        // Draw middle line
        //
        {
            pen_ptr pen{CreatePen(PS_SOLID, 1, RGB(255, 255, 255))};
            auto old_pen{select(hdc, pen)};

            const int y0 = mid_y - line_height/2;
            const int y1 = mid_y + line_height/2;

            MoveToEx(hdc, 0, y0, nullptr);
            LineTo(hdc, client_rect.right, y0);
            MoveToEx(hdc, 0, y1, nullptr);
            LineTo(hdc, client_rect.right, y1);
        }

        //
        // Offset and draw grid
        //
        const int y_offset = -mid_y + line_height * centered_row_;

        POINT prev_org;
        RECT paint_rect = paint_rect_;
        OffsetRect(&paint_rect, 0, y_offset);
        GetWindowOrgEx(hdc, &prev_org);
        SetWindowOrgEx(hdc, prev_org.x, prev_org.y + y_offset, nullptr);

        const int rows = grid_.rows();
        const auto colw = grid_.column_widths();
        
        const int row_min = std::max<int>(paint_rect.top / line_height, 0);
        const int row_max = std::min<int>((paint_rect.bottom + line_height - 1) / line_height, rows);

        for (int r = row_min; r < row_max; ++r) {
            int x = /*x_spacing*/0;
            for (int c = 0; c < static_cast<int>(colw.size()); ++c) {
                const auto s = grid_.cell_value(r, c);
                TextOutA(hdc, x, y_spacing + r * line_height - line_height/2, s.c_str(), static_cast<int>(s.length()));
                x += colw[c] * font_size.cx + x_spacing;
            }
        }

        //
        // Restore origin
        //

        SetWindowOrgEx(hdc, prev_org.x, prev_org.y, nullptr);
    }
};

text_grid_view text_grid_view::create(HWND parent_wnd, virtual_grid& grid) {
    return text_grid_view{text_grid_view_impl::create(parent_wnd, grid)->hwnd()};
}

void text_grid_view::centered_row(int row) {
    text_grid_view_impl::from_hwnd(hwnd())->centered_row(row);
}