#ifndef SAMPEDIT_WIN32_TEXT_GRID_H
#define SAMPEDIT_WIN32_TEXT_GRID_H

#include <win32/base.h>

class virtual_grid {
public:
    virtual ~virtual_grid() {}

    int rows() const {
        return do_rows();
    }

    std::vector<int> column_widths() const {
        return do_column_widths();
    }

    std::wstring cell_value(int row, int column) const {
        return do_cell_value(row, column);
    }

private:
    virtual int do_rows() const = 0;
    virtual std::vector<int> do_column_widths() const = 0;
    virtual std::wstring do_cell_value(int row, int column) const = 0;
};


class text_grid_view {
public:
    static text_grid_view create(HWND parent_wnd, virtual_grid& grid);
    explicit text_grid_view() : hwnd_(nullptr) {}

    HWND hwnd() const { return hwnd_; }

    void centered_row(int row);

private:
    explicit text_grid_view(HWND hwnd) : hwnd_(hwnd) {}

    HWND hwnd_;
};

#endif