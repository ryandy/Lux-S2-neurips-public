#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RolePincer : Role {
    struct Factory *factory;
    struct Unit *target_unit;

    struct Unit *oscillating_unit;
    struct Unit *partner_unit;
    struct Cell *stage_cell;
    struct Cell *target_cell1;
    struct Cell *target_cell2;
    struct Cell *attack_cell;

    // ~~~ Methods:

    RolePincer(struct Unit *_unit, struct Factory *_factory, struct Unit *_target_unit,
               struct Unit *_partner_unit, struct Cell *_stage_cell,
               struct Cell *_target_cell1, struct Cell *_target_cell2,
               std::vector<struct Cell*> *_route);
    static inline RolePincer *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RolePincer))
                ? static_cast<struct RolePincer*>(role) : NULL); }

    static void transition_units();
    static bool get_routes(struct Unit *target_unit, struct Unit *u1, struct Unit *u2,
                           std::vector<struct Cell*> *route1, std::vector<struct Cell*> *route2);

    bool primary();

    void print(std::ostream& os) const;
    struct RolePincer *copy() { return new RolePincer(*this); }
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
} RolePincer;
