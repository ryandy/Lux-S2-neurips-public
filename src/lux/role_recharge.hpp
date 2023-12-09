#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleRecharge : Role {
    struct Factory *factory;

    // ~~~ Methods:

    RoleRecharge(struct Unit *u, struct Factory *f) : Role(u, 'f'), factory(f) {};
    static inline RoleRecharge *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleRecharge))
                ? static_cast<struct RoleRecharge*>(role) : NULL); }

    static bool from_unit(struct Role **new_role, struct Unit *_unit);

    static bool from_transition_low_power(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_low_water(struct Role **new_role, struct Unit *_unit);

    bool _do_ice_conflict_power_transfer();

    void print(std::ostream& os) const;
    struct RoleRecharge *copy() { return new RoleRecharge(*this); }
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
} RoleRecharge;
