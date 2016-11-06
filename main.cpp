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

#include "module.h"

std::vector<float> create_sample(int len, float freq, int rate=44100)
{
    std::vector<float> data(len);
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < len; ++i) {
        data[i] = std::cosf(2*pi*freq*i/rate);
    }
    return data;
}

std::vector<float> convert_sample_data(const std::vector<signed char>& d) {
    std::vector<float> data(d.size());
    for (size_t i = 0, len = d.size(); i < len; ++i) {
        data[i] = d[i]/128.0f;
    }
    return data;
}

int main(int argc, char* argv[])
{
    try {
        std::vector<sample> samples;
        if (argc > 1) {
            auto mod = load_module(argv[1]);
            wprintf(L"Loaded '%S' - '%22.22S'\n", argv[1], mod.name);
            for (int i = 0; i < 31; ++i) {
                const auto& s = mod.samples[i];
                samples.emplace_back(convert_sample_data(s.data), std::string(s.name, s.name+sizeof(s.name)));
                if (s.loop_length > 2) {
                    samples.back().loop_start(s.loop_start);
                    samples.back().loop_length(s.loop_length);
                }
            }
        } else {
            for (int i = 1; i <= 4; ++i) { 
                samples.emplace_back(create_sample(44100/4, 440.0f * i), "Sample " + std::to_string(i));
            }
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
    } catch (const std::runtime_error& e) {
        wprintf(L"%S\n", e.what());
    }
    return 1;
}