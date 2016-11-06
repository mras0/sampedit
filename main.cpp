#include <stdio.h>
#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

#include <win32/base.h>
#include <win32/gdi.h>
#include <win32/sample_window.h>
#include <win32/main_window.h>

std::vector<float> create_sample(int len, float freq, int rate=44100)
{
    std::vector<float> data(len);
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < len; ++i) {
        data[i] = std::cosf(2*pi*freq*i/rate);
    }
    return data;
}

int main()
{
    std::vector<sample> samples;
    for (int i = 1; i <= 4; ++i) { 
        samples.emplace_back(create_sample(44100/4, 440.0f * i), "Sample " + std::to_string(i));
    }
    auto main_wnd = main_window::create();
    main_wnd.set_samples(samples);

    ShowWindow(main_wnd.hwnd(), SW_SHOW);
    UpdateWindow(main_wnd.hwnd());
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}