#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleRelocate : Role {
    struct Factory *factory;
    struct Factory *target_factory;

    // ~~~ Methods:

    RoleRelocate(struct Unit *_unit, struct Factory *_factory, struct Factory *_target_factory);
    static inline RoleRelocate *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleRelocate))
                ? static_cast<struct RoleRelocate*>(role) : NULL); }

    static bool from_idle(struct Role **new_role, struct Unit *_unit);
    static bool from_ore_surplus(struct Role **new_role, struct Unit *_unit);
    static bool from_power_surplus(struct Role **new_role, struct Unit *_unit);
    static bool from_assist_ice_conflict(struct Role **new_role, struct Unit *_unit);

    static bool from_transition_assist_ice_conflict(struct Role **new_role, struct Unit *_unit);

    void print(std::ostream& os) const;
    struct RoleRelocate *copy() { return new RoleRelocate(*this); }
    struct Factory *get_factory();
    double power_usage();
    void set();
    void unset();
    void teardown();
    bool is_valid();
    struct Cell *goal_cell();
    void update_goal();
    bool do_move();
    bool do_dig();
    bool do_transfer();
    bool do_pickup();
} RoleRelocate;
