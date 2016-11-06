#include <win32/main_window.h>
#include <win32/sample_window.h>
#include <win32/gdi.h>
#include <sstream>
#include <iomanip>

class main_window_impl : public window_base<main_window_impl> {
public:
    ~main_window_impl() = default;

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
    friend window_base<main_window_impl>;

    explicit main_window_impl() 
        : background_brush_(create_background_brush())
        , font_(create_default_font(info_font_height)) {
    }

    static const wchar_t* class_name() { return L"main_window"; }

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
                SetWindowText(hwnd(), L"SampEdit");
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

main_window main_window::create()
{
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