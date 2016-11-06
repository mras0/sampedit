#include <win32/gdi.h>

font_ptr create_default_font(int height)
{
    const wchar_t* const face_name = L"MS Shell Dlg 2";
    HFONT font = CreateFont(height, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE, face_name);
    if (!font) {
        fatal_error(L"CreateFont");
    }
    return font_ptr{font};
}