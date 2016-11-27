#include "info_window.h"
#include <win32/gdi.h>
#include <sstream>
#include <iomanip>

class info_window_impl : public window_base<info_window_impl> {
public:
    void set_module(const module& mod) {
        mod_ = &mod;
        pos_ = module_position{};
        update_info();
        update_pos();
    }

    void position_changed(const module_position& pos) {
        assert(mod_);
        pos_ = pos;
        update_pos();
    }
private:
    friend window_base<info_window_impl>;
    static const wchar_t* class_name() { return L"info_window_impl"; }
    font_ptr        font_;
    HWND            pos_label_wnd_;
    HWND            info_label_wnd_;
    const module*   mod_ = nullptr;
    module_position pos_;
    static constexpr int font_height_ = 12;

    explicit info_window_impl() {
    }

    void update_pos() {
        std::wstringstream wss;
        wss << "Order: " << pos_.order << " Pattern: " << pos_.pattern << " Row: " << pos_.row << "\n";
        SetWindowText(pos_label_wnd_, wss.str().c_str());
    }

    void update_info() {
        assert(mod_);
        std::wstringstream wss;
        wss << mod_->num_channels << " channel " << module_type_name[static_cast<int>(mod_->type)] << " module name: " << mod_->name.c_str() << ", initial speed " << mod_->initial_speed << ", initial tempo " << mod_->initial_tempo << "\n";
        wss << "# \tVolume \t";
        if (mod_->type == module_type::xm) {
            wss << "Fadeout \tRelNote \t";
        }
        wss << "C5Rate \tLength \tLoop \t\tEnd \tName\n";
        wss << std::setfill(L'0') << std::uppercase;
        for (size_t i = 0; i < mod_->instruments.size(); ++i) {
            const auto& inst = mod_->instruments[i];
            wss << std::dec << std::setw(2) << (i+1) << " \t";
            wss << std::hex << std::setw(2) << inst.volume << " \t\t";
            if (mod_->type == module_type::xm) {
                wss << std::hex << std::setw(4) << inst.volume_fadeout << " \t\t";
                wss << std::dec << std::setw(2) << inst.relative_note << " \t\t";
            }
            wss << std::dec;
            constexpr int len_w = 8;
            const auto& s = inst.samp;
            wss << std::setfill(L' ') << std::setw(5) << std::dec << static_cast<int>(0.5f+inst.c5_rate()) << std::setfill(L'0') << " \t\t";
            wss << std::setw(len_w) << std::hex << s.length() << " \t";
            wss << std::setw(len_w) << s.loop_start() << " \t";
            wss << std::setw(len_w) << s.loop_length() << " \t";
            wss << s.name().c_str() << "\n";
        }
        SetWindowText(info_label_wnd_, wss.str().c_str());
    }

    HWND create_label() {
        HWND const wnd = CreateWindow(L"STATIC", L"", WS_CHILD|WS_VISIBLE, 0, 0, 100, 100, hwnd(), nullptr, nullptr, nullptr);
        if (!wnd) {
            fatal_error(L"CreateWindow");
        }
        set_font(wnd, font_);
        return wnd;
    }

    bool on_create() {
        font_ = create_default_font(font_height_);
        pos_label_wnd_  = create_label();
        info_label_wnd_ = create_label();
        return true;
    }

    void on_size(unsigned /*state*/, const int cx, const int cy) {
        const int pos_label_height = font_height_;
        SetWindowPos(pos_label_wnd_,  nullptr, 0, 0, cx, pos_label_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(info_label_wnd_,  nullptr, 0, pos_label_height, cx, cy - pos_label_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HBRUSH on_color_static(HDC hdc, HWND /*static_wnd*/) {
        SetTextColor(hdc, default_text_color);
        SetBkColor(hdc, default_background_color);
        return GetStockBrush(HOLLOW_BRUSH);//background_brush_.get();
    }
};

info_window info_window::create(HWND parent_wnd) {
    return info_window{info_window_impl::create(parent_wnd)->hwnd()};
}

void info_window::set_module(const module& mod) {
    info_window_impl::from_hwnd(hwnd())->set_module(mod);
}

void info_window::position_changed(const module_position& pos) {
    info_window_impl::from_hwnd(hwnd())->position_changed(pos);
}