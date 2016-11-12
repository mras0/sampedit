#include "pattern_edit.h"
#include <win32/text_grid.h>

pattern_edit pattern_edit::create(HWND parent_wnd, virtual_grid& grid) {
    return pattern_edit{text_grid_view::create(parent_wnd, grid).hwnd()};
}