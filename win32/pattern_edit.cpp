#include "pattern_edit.h"
#include <win32/text_grid.h>

#include <iomanip>
#include <sstream>
class test_grid : public virtual_grid {
public:
private:
    virtual int do_rows() const override {
        return 64;
    }
    virtual std::vector<int> do_column_widths() const override {
        return { 3, 9, 9, 9, 9 };
    }
    virtual std::wstring do_cell_value(int row, int column) const override {
        std::wostringstream wss;
        wss << std::hex << std::setfill(L'0');
        if (column == 0) {
            wss << std::setw(2) << row;
        } else {
            wss << std::setw(2) << column;
        }
        return wss.str();
    }
};

pattern_edit pattern_edit::create(HWND parent_wnd) {
    static test_grid grid;
    return pattern_edit{text_grid_view::create(parent_wnd, grid).hwnd()};
}