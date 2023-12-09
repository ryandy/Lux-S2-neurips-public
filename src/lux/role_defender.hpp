#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleDefender : Role {
    struct Factory *factory;
    struct Cell *target_cell;

    // ~~~ Methods:

    RoleDefender(struct Unit *_unit, struct Factory *_factory, struct Cell *_target_cell);
    static inline RoleDefender *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleDefender))
                ? static_cast<struct RoleDefender*>(role) : NULL); }

    static bool from_unit(struct Role **new_role, struct Unit *_unit, int max_dist);

    void print(std::ostream& os) const;
    struct RoleDefender *copy() { return new RoleDefender(*this); }
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
} RoleDefender;
