#ifndef SAMPEDIT_MOD_PLAYER_H
#define SAMPEDIT_MOD_PLAYER_H

#include <memory>
#include <base/event.h>
#include "module.h"

class mixer;

class mod_player {
public:
    explicit mod_player(module&& mod, mixer& m);
    ~mod_player();

    const module& mod() const;

    void skip_to_order(int order);

    void stop();
    void toggle_playing();

    void on_position_changed(const callback_function_type<module_position>& cb);

    static constexpr int max_rows     = 64;
    static constexpr int max_volume   = 64;

    class impl;
private:
    std::unique_ptr<impl> impl_;
};

#endif