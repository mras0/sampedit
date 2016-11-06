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

    void select_sample(int index) {
        std::wostringstream wss;
        if (samples_ && index >= 0 && index < static_cast<int>(samples_->size())) {
            const auto& s = (*samples_)[index];
            sample_wnd_.set_sample(&s);
            wss << std::setw(2) << index+1 << ": " << s.length() << " - \"" << s.name().c_str() << "\"\n";
        } else {
            sample_wnd_.set_sample(nullptr);
            wss << "No sample selected\n";
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
            } else if (wparam >= '0' && wparam <= '9') {
                int selected = static_cast<int>(wparam - '0');
                if (selected == 0) selected = 10;
                select_sample(selected - 1);
            }
            break;

        case WM_SIZE: {
                const int w      = GET_X_LPARAM(lparam);
                const int s_h    = 400;
                const int text_w = 150;
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

void main_window::set_samples(const std::vector<sample>& s)
{
    main_window_impl::from_hwnd(hwnd())->set_samples(s);
}