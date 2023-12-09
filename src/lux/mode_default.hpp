#pragma once

#include "lux/mode.hpp"

#include <cstddef>  // NULL


typedef struct ModeDefault : Mode {
    ModeDefault(struct Factory *_factory) : Mode(_factory) {}
    static inline ModeDefault *cast(struct Mode *mode) {
        return ((mode && typeid(*mode) == typeid(struct ModeDefault))
                ? static_cast<struct ModeDefault*>(mode) : NULL); }

    static bool from_factory(struct Mode **new_mode, struct Factory *_factory);

    // Standard Mode methods
    void print(std::ostream& os) const;
    struct ModeDefault *copy() { return new ModeDefault(*this); }
    void set();
    void unset();
    bool is_valid();
    struct Role *get_transition_role(struct Unit *unit);
    struct Role *get_new_role(struct Unit *unit);
    bool build_heavy_next();
    bool do_build();
    bool do_water();
} ModeDefault;
