#ifndef SAMPEDIT_WIN32_TEXT_GRID_H
#define SAMPEDIT_WIN32_TEXT_GRID_H

#include <win32/base.h>
#include <base/virtual_grid.h>

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