#include <win32/sample_window.h>
#include <win32/gdi.h>

class popup_menu {
public:
    explicit popup_menu() : menu_(CreatePopupMenu()) {
    }
    popup_menu(const popup_menu&) = delete;
    popup_menu& operator=(const popup_menu&) = delete;
    ~popup_menu() {
        DestroyMenu(menu_);
    }

    void insert(UINT_PTR id, const wchar_t* text) {
        InsertMenu(menu_, static_cast<UINT>(-1), MF_BYPOSITION|MF_STRING, id, text);
    }

    void track(int x, int y, HWND parent_wnd) {
        TrackPopupMenu(menu_, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, x, y, 0, parent_wnd, nullptr);
    }

private:
    HMENU menu_;
};

class sample_window_impl : public window_base<sample_window_impl>, private double_buffered_paint<sample_window_impl> {
public:
    ~sample_window_impl() = default;

    void set_sample(const sample* s) {
        sample_    = s;
        selection_ = sample_range{};
        undo_zoom();
    }

    void on_zoom_change(const callback_function_type<sample_range>& cb) {
        on_zoom_change_.subscribe(cb);
        cb(zoom_);
    }

    void on_selection_change(const callback_function_type<sample_range>& cb) {
        on_selection_change_.subscribe(cb);
        cb(selection_);
    }

private:
    friend window_base<sample_window_impl>;
    friend double_buffered_paint<sample_window_impl>;

    enum menu_ids {
        menu_id_zoom = 100,
        menu_id_undo_zoom,
    };

    explicit sample_window_impl() : background_brush_(create_background_brush()) {
        menu_.insert(menu_id_zoom, L"Zoom");
        menu_.insert(menu_id_undo_zoom, L"Undo Zoom");
    }

    popup_menu    menu_;
    brush_ptr     background_brush_;

    POINT         size_;
    const sample* sample_ = nullptr;
    sample_range  zoom_;
    sample_range  selection_;

    event<sample_range> on_zoom_change_;
    event<sample_range> on_selection_change_;

    enum class state {
        normal,
        selecting,
    } state_ = state::normal;

    static const wchar_t* class_name() { return L"sample_window"; }

    const int x_border = 10;
    const int y_border = 10;

    void undo_zoom() {
        assert(state_ == state::normal);
        zoom_ = sample_range{0, sample_ ? sample_->length() : 0};
        InvalidateRect(hwnd(), nullptr, TRUE);
        on_zoom_change_(zoom_);
    }

    int sample_pos_to_x(int pos) const {
        return (pos - zoom_.x0) * (size_.x - 2 * x_border) / zoom_.size();
    }

    int x_to_sample_pos(int x) const {
        return zoom_.x0 + x * zoom_.size() / (size_.x - 2 * x_border);
    }

    int sample_val_to_y(float val) const {
        return (size_.y / 2) + static_cast<int>(0.5 + val * (size_.y / 2 - y_border));
    }

    void paint(HDC hdc, const RECT& paint_rect) {
        FillRect(hdc, &paint_rect, background_brush_.get());
        if (size_.x <= 2*x_border || size_.y <= 2*y_border) return;
        if (!sample_ || !zoom_.valid()) return;

        SetBkColor(hdc, default_background_color);

        //
        // Sample data
        //
        {
            pen_ptr pen{CreatePen(PS_SOLID, 1, RGB(255, 0, 0))};
            auto old_pen{select(hdc, pen)};

            bool first = true;
            for (int x = x_border; x < size_.x - x_border; ++x) {
                const int y = sample_val_to_y(sample_->get(x_to_sample_pos(x-x_border)));
                if (first) {
                    MoveToEx(hdc, x, y, nullptr);
                    first = false;
                } else {
                    LineTo(hdc, x, y);
                }
            }
        }

        //
        // Middle line
        //
        {
            pen_ptr dotted_pen{CreatePen(PS_DOT, 1, RGB(255, 255, 255))};
            auto old_pen_2{select(hdc, dotted_pen)};
            MoveToEx(hdc, 0, size_.y / 2, nullptr);
            LineTo(hdc, size_.x, size_.y / 2);
        }

        //
        // Loop lines
        //
        if (sample_->loop_length()) {
            pen_ptr pen{CreatePen(PS_SOLID, 1, RGB(255, 255, 255))};
            auto old_pen_3{select(hdc, pen)};
            const int start_x = sample_pos_to_x(sample_->loop_start());
            const int end_x   = sample_pos_to_x(sample_->loop_start() + sample_->loop_length());
            MoveToEx(hdc, start_x, 0, nullptr);
            LineTo(hdc, start_x, size_.y);
            MoveToEx(hdc, end_x, 0, nullptr);
            LineTo(hdc, end_x, size_.y);
        }


        //
        // Selection
        //
        const RECT selection_rect = {
            x_border + sample_pos_to_x(selection_.x0), y_border, 
            x_border + sample_pos_to_x(selection_.x1), size_.y - y_border };
        InvertRect(hdc, &selection_rect);
    }

    void on_size(UINT /*state*/, int cx, int cy) {
        size_ = POINT{cx, cy};
    }

    LRESULT wndproc(UINT umsg, WPARAM wparam, LPARAM lparam) {
        switch (umsg) {
        case WM_ERASEBKGND:
            return TRUE;

        case WM_LBUTTONDOWN:
            assert(state_ == state::normal);
            if (sample_) {
                state_ = state::selecting;
                selection_.x0 = selection_.x1 = zoom_.clamp(x_to_sample_pos(GET_X_LPARAM(lparam)));
                on_selection_change_(selection_);
                SetCapture(hwnd());
                InvalidateRect(hwnd(), nullptr, TRUE);
            }
            break;

        case WM_LBUTTONUP:
            if (state_ == state::selecting) {
                assert(sample_);
                ReleaseCapture();
                state_ = state::normal;
                if (selection_.x1 < selection_.x0) {
                    std::swap(selection_.x0, selection_.x1);
                }
            }
            break;

        case WM_MOUSEMOVE:
            if (state_ == state::selecting) {
                assert(sample_);
                const auto new_end = zoom_.clamp(x_to_sample_pos(GET_X_LPARAM(lparam)));
                if (selection_.x1 != new_end) {
                    selection_.x1 = new_end;
                    on_selection_change_(sample_range{std::min(selection_.x0, selection_.x1), std::max(selection_.x0, selection_.x1)});
                    InvalidateRect(hwnd(), nullptr, TRUE);
                }
            }
            break;

        case WM_RBUTTONUP: {
                RECT window_rect;
                GetWindowRect(hwnd(), &window_rect);
                menu_.track(window_rect.left + GET_X_LPARAM(lparam), window_rect.top + GET_Y_LPARAM(lparam), hwnd());
            }
            break;

        case WM_COMMAND:
            assert(state_ == state::normal);
            switch (LOWORD(wparam)) {
            case menu_id_zoom:
                if (selection_.valid()) {
                    zoom_ = selection_;
                    selection_ = sample_range{};
                    InvalidateRect(hwnd(), nullptr, TRUE);
                    on_zoom_change_(zoom_);
                    on_selection_change_(selection_);
                }
                break;
            case menu_id_undo_zoom:
                undo_zoom();
                break;
            }
            break;
        }
        return DefWindowProc(hwnd(), umsg, wparam, lparam);
    }
};

sample_window sample_window::create(HWND parent_wnd)
{
    return sample_window{sample_window_impl::create(parent_wnd)->hwnd()};
}

void sample_window::set_sample(const sample* s)
{
    assert(hwnd());
    return sample_window_impl::from_hwnd(hwnd())->set_sample(s);
}

void sample_window::on_zoom_change(const callback_function_type<sample_range>& cb)
{
    sample_window_impl::from_hwnd(hwnd())->on_zoom_change(cb);
}

void sample_window::on_selection_change(const callback_function_type<sample_range>& cb)
{
    sample_window_impl::from_hwnd(hwnd())->on_selection_change(cb);
}