#pragma once

#include "lux/mode.hpp"

#include <cstddef>  // NULL


typedef struct ModeIceConflict : Mode {
    struct Factory *opp_factory;
    bool offensive;
    bool defensive;
    bool desperate;

    // ~~~ Methods:

    ModeIceConflict(struct Factory *_factory, struct Factory *_opp_factory,
                    bool _defensive = false, bool _desperate = false);
    static inline ModeIceConflict *cast(struct Mode *mode) {
        return ((mode && typeid(*mode) == typeid(struct ModeIceConflict))
                ? static_cast<struct ModeIceConflict*>(mode) : NULL); }

    static bool from_transition_antagonized(struct Mode **new_mode, struct Factory *_factory);

    static bool from_ice_superiority(struct Mode **new_mode, struct Factory *_factory);
    static bool from_desperation(struct Mode **new_mode, struct Factory *_factory,
                                 struct Factory *attacking_factory = NULL);

    // Standard Mode methods
    void print(std::ostream& os) const;
    struct ModeIceConflict *copy() { return new ModeIceConflict(*this); }
    void set();
    void unset();
    bool is_valid();
    struct Role *get_transition_role(struct Unit *unit);
    struct Role *get_new_role(struct Unit *unit);
    bool build_heavy_next();
    bool do_build();
    bool do_water();
} ModeIceConflict;
