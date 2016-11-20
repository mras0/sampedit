#include <win32/main_window.h>
#include <win32/sample_window.h>
#include <win32/pattern_edit.h>
#include <win32/gdi.h>
#include <sstream>
#include <iomanip>

// Enable v6 common controls
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>

void set_font(HWND window, const font_ptr& font) {
    assert(font);
    SendMessage(window, WM_SETFONT, reinterpret_cast<WPARAM>(font.get()), 0);
}

class sample_edit : public window_base<sample_edit> {
public:
    ~sample_edit() = default;

    void set_samples(const std::vector<sample>& s) {
        samples_ = &s;
        select_sample(0);
    }

    void on_piano_key_pressed(const callback_function_type<piano_key>& cb) {
        on_piano_key_pressed_.subscribe(cb);
    }

    int current_sample_index() const {
        return sample_index_;
    }

private:
    friend window_base<sample_edit>;

    explicit sample_edit() 
        : background_brush_(create_background_brush())
        , font_(create_default_font(info_font_height)) {
    }

    static const wchar_t* class_name() { return L"sample_edit"; }

    const std::vector<sample>* samples_ = nullptr;

    static constexpr int info_font_height = 12;

    brush_ptr                  background_brush_;
    font_ptr                   font_;
    sample_window              sample_wnd_;
    HWND                       sample_info_wnd_;
    HWND                       zoom_info_wnd_;
    HWND                       selection_info_wnd_;
    int                        sample_index_= -1;

    event<piano_key>           on_piano_key_pressed_;

    void select_sample(int index) {
        std::wostringstream wss;
        if (samples_ && index >= 0 && index < static_cast<int>(samples_->size())) {
            const auto& s = (*samples_)[index];
            sample_wnd_.set_sample(&s);
            wss << std::setw(2) << index+1 << ": " << s.length() << " - \"" << s.name().c_str() << "\"\n";
            sample_index_ = index;
        } else {
            sample_wnd_.set_sample(nullptr);
            wss << "No sample selected\n";
            sample_index_ = -1;
        }
        SetWindowText(sample_info_wnd_, wss.str().c_str());
        InvalidateRect(hwnd(), nullptr, TRUE);
    }

    HWND create_label() {
        HWND label_wnd = CreateWindow(L"STATIC", L"", WS_CHILD|WS_VISIBLE, 0, 0, 400, 100, hwnd(), nullptr, nullptr, nullptr);
        if (!label_wnd) {
            fatal_error(L"CreateWindow");
        }
        set_font(label_wnd, font_);
        return label_wnd;
    }

    bool on_create() {
        sample_wnd_      = sample_window::create(hwnd());
        sample_info_wnd_ = create_label();
        zoom_info_wnd_   = create_label();
        sample_wnd_.on_zoom_change([this](sample_range r) {
            std::wostringstream wss;
            wss << "Zoom " << r.x0 << ", " << r.x1;
            SetWindowText(zoom_info_wnd_, wss.str().c_str());
        });
        selection_info_wnd_ = create_label();
        sample_wnd_.on_selection_change([this](sample_range r) {
            std::wostringstream wss;
            wss << "Selection " << r.x0 << ", " << r.x1;
            SetWindowText(selection_info_wnd_, wss.str().c_str());
        });
        return true;
    }

    void on_size(UINT /*state*/, int cx, int /*cy*/) {
        constexpr int s_h         = 400;
        constexpr int text_w      = 200;
        constexpr int text_offset = 10;
        SetWindowPos(sample_wnd_.hwnd(), nullptr, 0, 0, cx, s_h, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(sample_info_wnd_, nullptr, text_offset, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(zoom_info_wnd_, nullptr, text_w+text_offset*2, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(selection_info_wnd_, nullptr, text_w*2+text_offset*3, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HBRUSH on_color_static(HDC hdc, HWND /*static_wnd*/) {
        SetTextColor(hdc, default_text_color);
        SetBkColor(hdc, default_background_color);
        return background_brush_.get();
    }

    void on_key_down(int vk, unsigned /*extra*/) {
        if (vk == VK_SPACE) {
            on_piano_key_pressed_(piano_key::OFF);
        } else if (vk == VK_OEM_PLUS) {
            if (samples_) {
                assert(sample_index_ >= 0);
                select_sample((sample_index_ + 1) % samples_->size());
            }
        } else if (vk == VK_OEM_MINUS) {
            if (samples_) {
                assert(sample_index_ >= 0);
                select_sample(sample_index_ ? sample_index_ - 1 : static_cast<int>(samples_->size()) - 1);
            }
        } else {
            piano_key key = key_to_note(vk);
            if (key != piano_key::NONE) {
                on_piano_key_pressed_(key);
            }
        }
    }
};

class main_window_impl : public window_base<main_window_impl> {
public:
    ~main_window_impl() = default;

    void set_module(const module& mod) {
        sample_edit_->set_samples(mod.samples);
        pattern_edit_.set_module(mod);
    }

    void on_piano_key_pressed(const callback_function_type<piano_key>& cb) {
        sample_edit_->on_piano_key_pressed(cb);
    }

    void on_start_stop(const callback_function_type<>& cb) {
        on_start_stop_.subscribe(cb);
    }

    int current_sample_index() const {
        return sample_edit_->current_sample_index();
    }

    void position_changed(const module_position& pos) {    
        pattern_edit_.position_changed(pos);
    }

    void on_order_selected(const callback_function_type<int>& cb) {
        pattern_edit_.on_order_selected(cb);
    }

private:
    friend window_base<main_window_impl>;
    virtual_grid& grid_;
    event<>   on_start_stop_;

    explicit main_window_impl(virtual_grid& grid) : grid_(grid), tab_font_(create_default_font(14)) {
    }

    static const wchar_t* class_name() { return L"main_window"; }

    //
    // Tabs
    //
    HWND              tab_control_wnd_;
    font_ptr          tab_font_;
    std::vector<HWND> tab_pages_;

    void add_tab_page(const wchar_t* text, HWND wnd) {
        const auto page = static_cast<int>(tab_pages_.size());
        TCITEM tci;
        tci.mask    = TCIF_TEXT;
        tci.pszText = const_cast<wchar_t*>(text);
        if (TabCtrl_InsertItem(tab_control_wnd_, page, &tci) == -1) {
            fatal_error(L"TabCtrl_InsertItem");
        }
        ShowWindow(wnd, page == TabCtrl_GetCurSel(tab_control_wnd_) ? SW_SHOW : SW_HIDE);
        tab_pages_.push_back(wnd);
    }

    HWND current_tab_wnd() {
        const int page = TabCtrl_GetCurSel(tab_control_wnd_);
        assert(page >= 0 && page < tab_pages_.size());
        return tab_pages_[page];
    }

    bool on_tab_page_switching() {
        ShowWindow(current_tab_wnd(), SW_HIDE);
        return false; // false = allow tab switch
    }

    void on_tab_page_switched() {
        const int page = TabCtrl_GetCurSel(tab_control_wnd_);
        assert(page >= 0 && page < tab_pages_.size());
        RECT rect;
        GetClientRect(tab_control_wnd_, &rect);
        TabCtrl_AdjustRect(tab_control_wnd_, FALSE, &rect);
        HWND const tab_wnd = current_tab_wnd();
        SetWindowPos(tab_wnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(tab_wnd, SW_SHOW);
        InvalidateRect(tab_wnd, nullptr, TRUE);
    }

    void set_tab_page(int page) {
        if (page != TabCtrl_GetCurSel(tab_control_wnd_) && page < static_cast<int>(tab_pages_.size())) {
            if (!on_tab_page_switching()) {
                TabCtrl_SetCurSel(tab_control_wnd_, page);
                on_tab_page_switched();
            }
        }
    }

    //
    // Tab pages
    //
    pattern_edit   pattern_edit_;
    sample_edit*   sample_edit_;

    bool on_create() {
        SetWindowText(hwnd(), L"SampEdit");
        if ((tab_control_wnd_ = CreateWindow(WC_TABCONTROL, L"", WS_CHILD|WS_CLIPSIBLINGS|WS_VISIBLE, 0, 0, 400, 100, hwnd(), nullptr, nullptr, nullptr)) == nullptr) {
            fatal_error(L"CreateWindow(WC_TABCONTROL)");
        }
        set_font(tab_control_wnd_, tab_font_);
        sample_edit_  = sample_edit::create(tab_control_wnd_);
        pattern_edit_ = pattern_edit::create(tab_control_wnd_, grid_);
        add_tab_page(L"Pattern", pattern_edit_.hwnd());
        add_tab_page(L"Sample", sample_edit_->hwnd());
        return true;
    }

    void on_destroy() {
        PostQuitMessage(0);
    }

    void on_size(UINT /*state*/, int cx, int cy) {
        SetWindowPos(tab_control_wnd_, nullptr, 0, 0, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
        on_tab_page_switched();
    }

    void on_key_down(int vk, unsigned extra) {
        switch (vk) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            set_tab_page((vk == '0' ? 10 : vk - '0') - 1);
            break;
        case VK_RETURN:
            on_start_stop_();
            break;
        case VK_ESCAPE:
            SendMessage(hwnd(), WM_CLOSE, 0, 0);
            break;
        default:
            SendMessage(current_tab_wnd(), WM_KEYDOWN, vk, extra);
        }
    }

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_NOTIFY:
            switch (reinterpret_cast<const NMHDR*>(lparam)->code) {
            case TCN_SELCHANGING:              
                return on_tab_page_switching();
            case TCN_SELCHANGE:
                on_tab_page_switched();
                return TRUE;
            }
            break;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

main_window main_window::create(virtual_grid& grid) {
    INITCOMMONCONTROLSEX icce{ sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES };
    if (!InitCommonControlsEx(&icce)) {
        fatal_error(L"InitCommonControlsEx");
    }

    return main_window{main_window_impl::create(nullptr, grid)->hwnd()};
}

int main_window::current_sample_index() const {
    return main_window_impl::from_hwnd(hwnd())->current_sample_index();
}

void main_window::set_module(const module& mod) {
    main_window_impl::from_hwnd(hwnd())->set_module(mod);
}

void main_window::on_piano_key_pressed(const callback_function_type<piano_key>& cb) {
    main_window_impl::from_hwnd(hwnd())->on_piano_key_pressed(cb);
}

void main_window::on_start_stop(const callback_function_type<>& cb) {
    main_window_impl::from_hwnd(hwnd())->on_start_stop(cb);
}

void main_window::on_order_selected(const callback_function_type<int>& cb) {
    main_window_impl::from_hwnd(hwnd())->on_order_selected(cb);
}

void main_window::position_changed(const module_position& pos) {
    main_window_impl::from_hwnd(hwnd())->position_changed(pos);
}