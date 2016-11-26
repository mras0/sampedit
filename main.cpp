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

#include <base/job_queue.h>
#include <base/sample_voice.h>
#include "module.h"
#include "mixer.h"
#include "mod_player.h"

std::vector<float> create_sample(int len, float freq, int rate=44100)
{
    std::vector<float> data(len);
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < len; ++i) {
        data[i] = std::cosf(2*pi*freq*i/rate);
    }
    return data;
}

class mod_like_grid : public virtual_grid {
public:
    virtual void do_order_change(int order) = 0;
};

#include <iomanip>
#include <sstream>

class test_grid : public mod_like_grid {
public:
    virtual void do_order_change(int) override {
    }

private:
    virtual int do_rows() const override {
        return 64;
    }
    virtual std::vector<int> do_column_widths() const override {
        return { 9, 9, 9, 9 };
    }
    virtual std::string do_cell_value(int row, int column) const override {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << '(' << std::setw(2) << column << ", " << std::setw(2) << row << ')';
        return ss.str();
    }
};

class mod_grid : public mod_like_grid {
public:
    explicit mod_grid(const module& mod) : mod_(mod) {
    }

    virtual void do_order_change(int order) override {
        if (order_ != order) {
            wprintf(L"Order %d, Pattern %d\n", order, mod_.order[order]);
            order_ = order;
        }
    }

private:
    const module& mod_;
    int order_ = 0;

    virtual int do_rows() const override {
        return 64;
    }

    virtual std::vector<int> do_column_widths() const override {
        return std::vector<int>(mod_.num_channels, mod_.type == module_type::mod ? 10 : 14);
    }

    virtual std::string do_cell_value(int row, int column) const override {
        assert(row >= 0 && row < mod_player::max_rows);
        assert(column >= 0 && column < mod_.num_channels);
        std::ostringstream ss;
        const auto& note = mod_.at(order_, row)[column];
        ss << std::setfill('0') << std::uppercase;
        if (note.note != piano_key::NONE) {
            ss << piano_key_to_string(note.note) << ' ';
        } else {
            ss << "... ";
        }
        if (note.instrument) {
            ss << std::setw(2) << (int)note.instrument << ' ';
        } else {
            ss << ".. ";
        }
        if (mod_.type != module_type::mod) {
            assert(mod_.type == module_type::s3m);
            if (note.volume != no_volume_byte) {
                assert(note.volume < 100);
                ss << 'v' << std::setw(2) << (int)note.volume << ' ';
            } else {
                ss << "... ";
            }
        }
        if (note.effect) {
            ss << std::hex;
            if (mod_.type == module_type::mod) {
                ss << std::setw(3) << (int)note.effect;
            } else {
                assert(mod_.type == module_type::s3m);
                ss << (char)((note.effect >> 8) + 'A' - 1);
                ss << std::setw(2) << (int)(note.effect & 0xff);
            }
        } else {
            ss << "...";
        }
        assert(ss.str().length() == column_widths()[column]);
        return ss.str();
    }
};

int main(int argc, char* argv[])
{
    try {
        mixer m;

        const module* mod_ = nullptr;
        std::unique_ptr<mod_player> mod_player_;
        std::unique_ptr<mod_like_grid> grid;
        if (argc > 1) {
            mod_player_.reset(new mod_player(load_module(argv[1]), m));
            auto& mod = mod_player_->mod();
            wprintf(L"Loaded '%S' - '%S' %d channels\n", argv[1], mod.name.c_str(), mod.num_channels);
            for (size_t i = 0; i < mod.samples.size(); ++i) {
                const auto& s = mod.samples[i];
                wprintf(L"%2.2d: %-28S c5 rate: %d\n", (int)(i+1), s.name().c_str(), (int)(s.c5_rate()+0.5f));
            }
            mod_ = &mod;
            grid.reset(new mod_grid{mod});
        } else {
            static module mod;
            mod.samples.emplace_back(create_sample(44100/4, piano_key_to_freq(piano_key::C_5)), 44100.0f, "Test sample");
            mod.order.push_back(0);
            grid.reset(new test_grid{});
            mod_ = &mod;
        }
        auto main_wnd = main_window::create(*grid);
        assert(mod_);
        main_wnd.set_module(*mod_);

        sample_voice keyboard_voice(m.sample_rate());
        keyboard_voice.volume(0.5f);
        m.at_next_tick([&] { m.add_voice(keyboard_voice); });
        main_wnd.on_piano_key_pressed([&](piano_key key) {
            assert(key != piano_key::NONE);
            if (key == piano_key::OFF) {
                m.at_next_tick([&] { keyboard_voice.key_off(); } );
                return;
            }
            const int idx = main_wnd.current_sample_index();
            if (idx < 0 || idx >= mod_->samples.size()) return;
            const auto freq = piano_key_to_freq(key, piano_key::C_5, mod_->samples[idx].c5_rate());
            wprintf(L"Playing %S at %f Hz\n", piano_key_to_string(key).c_str(), freq);
            m.at_next_tick([&, idx, freq] {
                keyboard_voice.freq(freq);
                keyboard_voice.play(mod_->samples[idx], 0); 
            });
        });
        main_wnd.on_start_stop([&]() {
            if (mod_player_) {
                mod_player_->toggle_playing();
            }
        });

        ShowWindow(main_wnd.hwnd(), SW_SHOW);
        UpdateWindow(main_wnd.hwnd());

        job_queue gui_jobs;
        const DWORD gui_thread_id = GetCurrentThreadId();
        auto add_gui_job = [&gui_jobs, gui_thread_id] (const job_queue::job_type& job) {
            gui_jobs.post(job);
            PostThreadMessage(gui_thread_id, WM_NULL, 0, 0);
        };

        bool exiting = false;
        if (mod_player_) {
            mod_player_->on_position_changed([&](const module_position& pos) {
                add_gui_job([&main_wnd, &grid, pos] {
                    grid->do_order_change(pos.order);
                    main_wnd.position_changed(pos);
                });
            });
            main_wnd.on_exiting([&]() {
                wprintf(L"Exiting\n");
                exiting = true;
                mod_player_->stop();
            });
            main_wnd.on_order_selected([&](int order) {
               mod_player_->skip_to_order(order);
            });
            const int skip_to = argc > 2 ? std::stoi(argv[2]) : 0;
            if (skip_to > 0) {
                mod_player_->skip_to_order(skip_to);
            }
            mod_player_->toggle_playing();
        }

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.hwnd == nullptr && msg.message == WM_NULL) {
                if (!exiting) {
                    gui_jobs.perform_all();
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        return static_cast<int>(msg.wParam);
    } catch (const std::runtime_error& e) {
        wprintf(L"%S\n", e.what());
    }
    return 1;
}