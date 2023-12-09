#pragma once

#include <iostream>


struct Factory;
struct Role;
struct Unit;

typedef struct Mode {
    struct Factory *factory;

    int _set_step;

    // ~~~ Methods:

    Mode(struct Factory *_factory);
    virtual ~Mode() = default;

    bool is_set();

    // Default implementations available to all modes
    struct Role *_get_transition_role(struct Unit *unit);
    struct Role *_get_new_role(struct Unit *unit);
    bool _build_heavy_next();
    bool _do_build();
    bool _do_water();

    virtual void print(std::ostream& os) const = 0;
    virtual struct Mode *copy() = 0;

    virtual void set();
    virtual void unset();

    virtual bool is_valid() = 0;

    virtual struct Role *get_transition_role(struct Unit *unit) = 0;
    virtual struct Role *get_new_role(struct Unit *unit) = 0;
    virtual bool build_heavy_next() = 0;
    virtual bool do_build() = 0;
    virtual bool do_water() = 0;

    friend std::ostream &operator<<(std::ostream &os, const struct Mode &m) {
        m.print(os); return os;
    }
} Mode;
