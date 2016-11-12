#include <win32/main_window.h>
#include <win32/sample_window.h>
#include <win32/gdi.h>
#include <sstream>
#include <iomanip>

// Enable v6 common controls
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <commctrl.h>

class sample_edit_window : public window_base<sample_edit_window> {
public:
    ~sample_edit_window() = default;

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
    friend window_base<sample_edit_window>;

    explicit sample_edit_window() 
        : background_brush_(create_background_brush())
        , font_(create_default_font(info_font_height)) {
    }

    static const wchar_t* class_name() { return L"sample_edit_window"; }

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

    HBRUSH on_color_static(HDC hdc, HWND /*static_wnd*/) {
        SetTextColor(hdc, default_text_color);
        SetBkColor(hdc, default_background_color);
        return background_brush_.get();
    }

    HWND create_label() {
        HWND label_wnd = CreateWindow(L"STATIC", L"", WS_CHILD|WS_VISIBLE, 0, 0, 400, 100, hwnd(), nullptr, nullptr, nullptr);
        if (!label_wnd) {
            fatal_error(L"CreateWindow");
        }
        SendMessage(label_wnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_.get()), 0);
        return label_wnd;
    }

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_CREATE: {
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
            }
            break;

        case WM_NCDESTROY:
            PostQuitMessage(0);
            break;

        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(on_color_static(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));

        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                SendMessage(hwnd(), WM_CLOSE, 0, 0);
            } else if (wparam == VK_SPACE) {
                on_piano_key_pressed_(piano_key::OFF);
            } else if (wparam == VK_OEM_PLUS) {
                if (samples_) {
                    assert(sample_index_ >= 0);
                    select_sample((sample_index_ + 1) % samples_->size());
                }
            } else if (wparam == VK_OEM_MINUS) {
                if (samples_) {
                    assert(sample_index_ >= 0);
                    select_sample(sample_index_ ? sample_index_ - 1 : static_cast<int>(samples_->size()) - 1);
                }
            } else {
                piano_key key = key_to_note(static_cast<int>(wparam));
                if (key != piano_key::OFF) {
                    on_piano_key_pressed_(key);
                } else {
                    //wprintf(L"Unhandled key %d (0x%X)\n", (int)wparam, (int)wparam);
                }
            }
            break;

        case WM_SIZE: {
                const int w      = GET_X_LPARAM(lparam);
                const int s_h    = 400;
                const int text_w = 200;
                const int text_offset = 10;
                SetWindowPos(sample_wnd_.hwnd(), nullptr, 0, 0, w, s_h, SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(sample_info_wnd_, nullptr, text_offset, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(zoom_info_wnd_, nullptr, text_w+text_offset*2, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(selection_info_wnd_, nullptr, text_w*2+text_offset*3, s_h, text_w, info_font_height, SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

class main_window_impl : public window_base<main_window_impl> {
public:
    ~main_window_impl() = default;

    void set_samples(const std::vector<sample>& s) {
        sample_edit_window_->set_samples(s);
    }

    void on_piano_key_pressed(const callback_function_type<piano_key>& cb) {
        sample_edit_window_->on_piano_key_pressed(cb);
    }

    int current_sample_index() const {
        return sample_edit_window_->current_sample_index();
    }

private:
    friend window_base<main_window_impl>;

    explicit main_window_impl() {
    }

    static const wchar_t* class_name() { return L"main_window"; }

    //
    // Tabs
    //
    HWND              tab_control_wnd_;
    std::vector<HWND> tab_pages_;

    void add_tab_page(const wchar_t* text, HWND wnd) {
        const auto page = static_cast<int>(tab_pages_.size());
        TCITEM tci;
        tci.mask    = TCIF_TEXT;
        tci.pszText = const_cast<wchar_t*>(text);
        if (TabCtrl_InsertItem(tab_control_wnd_, page, &tci) == -1) {
            fatal_error(L"TabCtrl_InsertItem");
        }
        ShowWindow(wnd, page == 0 ? SW_SHOW : SW_HIDE);
        tab_pages_.push_back(wnd);
    }

    void select_tab_page(int page) {
        assert(page >= 0 && page < tab_pages_.size());
        RECT rect;
        GetClientRect(tab_control_wnd_, &rect);
        TabCtrl_AdjustRect(tab_control_wnd_, FALSE, &rect);
        SetWindowPos(tab_pages_[page], nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(tab_pages_[page], SW_SHOW);
        SetFocus(tab_pages_[page]);
    }

    //
    // Tab pages
    //
    sample_edit_window* sample_edit_window_;

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_CREATE: {
                SetWindowText(hwnd(), L"SampEdit");
                if ((tab_control_wnd_ = CreateWindow(WC_TABCONTROL, L"", WS_CHILD|WS_CLIPSIBLINGS|WS_VISIBLE, 0, 0, 400, 100, hwnd(), nullptr, nullptr, nullptr)) == nullptr) {
                    fatal_error(L"CreateWindow(WC_TABCONTROL)");
                }
                sample_edit_window_ = sample_edit_window::create(tab_control_wnd_);
                add_tab_page(L"Track", CreateWindow(L"STATIC", L"Track", WS_CHILD|WS_VISIBLE, 0, 0, 400, 100, tab_control_wnd_, nullptr, nullptr, nullptr));
                add_tab_page(L"Sample", sample_edit_window_->hwnd());
            }
            break;

        case WM_NCDESTROY:
            PostQuitMessage(0);
            break;

        case WM_SIZE:
            SetWindowPos(tab_control_wnd_, nullptr, 0, 0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), SWP_NOZORDER | SWP_NOACTIVATE);
            select_tab_page(TabCtrl_GetCurSel(tab_control_wnd_));
            break;

        case WM_SETFOCUS: {
                const int page = TabCtrl_GetCurSel(tab_control_wnd_);
                assert(page >= 0 && page < tab_pages_.size());
                SetFocus(tab_pages_[page]);
            }
            break;


        case WM_NOTIFY:
            switch (reinterpret_cast<const NMHDR*>(lparam)->code) {
            case TCN_SELCHANGING: {
                    const int page = TabCtrl_GetCurSel(tab_control_wnd_);
                    assert(page >= 0 && page < tab_pages_.size());
                    ShowWindow(tab_pages_[page], SW_HIDE);
                    return FALSE; // Allow change
                }
            case TCN_SELCHANGE: {
                    select_tab_page(TabCtrl_GetCurSel(tab_control_wnd_));
                    return TRUE;
                }
            }
            break;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

main_window main_window::create()
{
    INITCOMMONCONTROLSEX icce{ sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES };
    if (!InitCommonControlsEx(&icce)) {
        fatal_error(L"InitCommonControlsEx");
    }

    return main_window{main_window_impl::create(nullptr)->hwnd()};
}

int main_window::current_sample_index() const
{
    return main_window_impl::from_hwnd(hwnd())->current_sample_index();
}

void main_window::set_samples(const std::vector<sample>& s)
{
    main_window_impl::from_hwnd(hwnd())->set_samples(s);
}

void main_window::on_piano_key_pressed(const callback_function_type<piano_key>& cb)
{
    main_window_impl::from_hwnd(hwnd())->on_piano_key_pressed(cb);
}