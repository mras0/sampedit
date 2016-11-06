#include <stdio.h>
#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

#include <win32/base.h>
#include <win32/gdi.h>
#include <win32/sample_window.h>

class main_window : public window_base<main_window> {
public:
    ~main_window() = default;

    void set_sample(const sample& s) {
        sample_wnd_.set_sample(s);
    }

private:
    friend window_base<main_window>;

    explicit main_window() = default;

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

std::vector<float> create_sample(int len, int rate=44100)
{
    std::vector<float> data(len);
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < len; ++i) {
        data[i] = std::cosf(2*pi*440.0f*i/rate);
    }
    return data;
}

int main()
{
    sample samp{create_sample(44100/4)};
    auto& main_wnd = *main_window::create(nullptr);
    main_wnd.set_sample(samp);

    ShowWindow(main_wnd.hwnd(), SW_SHOW);
    UpdateWindow(main_wnd.hwnd());
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}