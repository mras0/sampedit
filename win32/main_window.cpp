#include <win32/main_window.h>
#include <win32/sample_window.h>

class main_window_impl : public window_base<main_window_impl> {
public:
    ~main_window_impl() = default;

    void set_sample(const sample& s) {
        sample_wnd_.set_sample(s);
    }

private:
    friend window_base<main_window_impl>;

    explicit main_window_impl() = default;

    static const wchar_t* class_name() { return L"main_window"; }

    sample_window sample_wnd_;

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_CREATE: {
                SetWindowText(hwnd(), L"SampEdit");
                sample_wnd_ = sample_window::create(hwnd());
            }
                        break;

        case WM_NCDESTROY:
            PostQuitMessage(0);
            break;

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) SendMessage(hwnd(), WM_CLOSE, 0, 0);
            break;

        case WM_SIZE:
            SetWindowPos(sample_wnd_.hwnd(), nullptr, 0, 0, GET_X_LPARAM(lparam), /*GET_Y_LPARAM(lparam)*/400, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

main_window main_window::create()
{
    return main_window{main_window_impl::create(nullptr)->hwnd()};
}

void main_window::set_sample(const sample& s)
{
    main_window_impl::from_hwnd(hwnd())->set_sample(s);
}