#include "text_grid.h"
#include <win32/gdi.h>
#include <algorithm>

class text_grid_view_impl : public window_base<text_grid_view_impl>, public double_buffered_paint<text_grid_view_impl> {
public:
    void column_offset(int offset) {
        if (offset != column_offset_) {
            assert(offset >= 0 && offset < static_cast<int>(grid_.column_widths().size()));
            column_offset_ = offset;
            InvalidateRect(hwnd(), nullptr, TRUE);
        }
    }

    void centered_row(int row) {
        if (row != centered_row_) {
            centered_row_ = row;
            InvalidateRect(hwnd(), nullptr, TRUE);
        }
    }

private:
    virtual_grid&   grid_;
    font_ptr        font_;
    int             column_offset_ = 0;
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
        } else if (vk == VK_TAB) {
            const int num_cols = static_cast<int>(grid_.column_widths().size());
            if (GetKeyState(VK_SHIFT)) {
                column_offset(column_offset_ ? column_offset_ - 1 : num_cols - 1);
            } else {
                column_offset((column_offset_ + 1) % num_cols);
            }
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
        const SIZE font_size{tm.tmAveCharWidth, tm.tmHeight};

        const int x_spacing = font_size.cx*2;
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
        // Draw column headers and calculate colx[]
        //
        const auto colw = grid_.column_widths();
        const int row_label_width = 2*font_size.cx + x_spacing;
        std::vector<int> colx(colw.size());
        int col_min = -1, col_max = static_cast<int>(colw.size());
        for (int c = column_offset_, x = row_label_width; c < static_cast<int>(colw.size()); ++c) {
            const bool visible = x >= paint_rect_.left;
            if (col_min == -1 && visible) {
                col_min = c;
            }
            if (x >= paint_rect_.right) {
                col_max = c;
                break;
            }
            if (visible) {
                const char column_header[2] = { static_cast<char>(((c+1)/10)+'0'), static_cast<char>(((c+1)%10)+'0') };
                TextOutA(hdc, x + font_size.cx * (colw[c] - sizeof(column_header))/2, 0, column_header, sizeof(column_header));
            }
            colx[c] = x;
            x += colw[c] * font_size.cx + x_spacing;
        }
        //wprintf(L"col_min = %2d, col_max = %2d\n", col_min, col_max);
        assert(col_min >= 0 && col_min < col_max && col_max <= colw.size());

        //
        // Draw grid
        //
        RECT paint_rect = paint_rect_;
        paint_rect.top += line_height; // offset paint rectangle to avoid painting over the column headers
        paint_rect.bottom = std::max(paint_rect.bottom, paint_rect.top);
        SetTextAlign(hdc, TA_LEFT|TA_TOP);
        const int rows  = grid_.rows();
        const int row_offset = centered_row_ - mid_y / line_height;
        const int row_min = std::max<int>(paint_rect.top / line_height + row_offset, 0);
        const int row_max = std::min<int>((paint_rect.bottom + line_height - 1) / line_height + row_offset, rows);
        const bool row_label_visible = true; // TODO: only draw if needed
        //wprintf(L"row_offset = %+2d, row_min = %2d, row_max = %2d\n", row_offset, row_min, row_max);
        for (int r = row_min; r < row_max; ++r) {
            const int y = y_spacing + (r - row_offset) * line_height + (mid_y + line_height/2) % line_height;
            if (row_label_visible) {
                const char row_label[2] = { static_cast<char>((r/10)+'0'), static_cast<char>((r%10)+'0') };
                TextOutA(hdc, 0, y, row_label, sizeof(row_label));
            }
            for (int c = col_min; c < col_max; ++c) {
                const int x = colx[c];
                assert(x >= paint_rect.left && x < paint_rect.right);
                const auto s = grid_.cell_value(r, c);
                TextOutA(hdc, x, y, s.c_str(), static_cast<int>(s.length()));
            }
        }
    }
};

text_grid_view text_grid_view::create(HWND parent_wnd, virtual_grid& grid) {
    return text_grid_view{text_grid_view_impl::create(parent_wnd, grid)->hwnd()};
}

void text_grid_view::centered_row(int row) {
    text_grid_view_impl::from_hwnd(hwnd())->centered_row(row);
}