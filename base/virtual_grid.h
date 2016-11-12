#ifndef SAMPEDIT_BASE_VIRTUAL_GRID_H
#define SAMPEDIT_BASE_VIRTUAL_GRID_H

#include <string>

class virtual_grid {
public:
    virtual ~virtual_grid() {}

    int rows() const {
        return do_rows();
    }

    std::vector<int> column_widths() const {
        return do_column_widths();
    }

    std::string cell_value(int row, int column) const {
        return do_cell_value(row, column);
    }

private:
    virtual int do_rows() const = 0;
    virtual std::vector<int> do_column_widths() const = 0;
    virtual std::string do_cell_value(int row, int column) const = 0;
};

#endif