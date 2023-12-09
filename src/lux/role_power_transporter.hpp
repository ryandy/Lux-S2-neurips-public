#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RolePowerTransporter : Role {
    struct Cell *factory_cell;
    struct Unit *target_unit;

    // ~~~ Methods:

    RolePowerTransporter(struct Unit *_unit, struct Cell *_factory_cell, struct Unit *_target_unit);
    static inline RolePowerTransporter *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RolePowerTransporter))
                ? static_cast<struct RolePowerTransporter*>(role) : NULL); }

    static bool from_miner(struct Role **new_role, struct Unit *_unit);

    static bool from_transition_protector(struct Role **new_role, struct Unit *_unit);

    bool _do_excess_power_transfer();

    void print(std::ostream& os) const;
    struct RolePowerTransporter *copy() { return new RolePowerTransporter(*this); }
    struct Factory *get_factory();
    double power_usage() { return 0; }
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
} RolePowerTransporter;
