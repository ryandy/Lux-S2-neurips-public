#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleAttacker : Role {
    struct Factory *factory;
    struct Unit *target_unit;

    bool low_power_attack;
    bool defend;

    // ~~~ Methods:

    RoleAttacker(struct Unit *_unit, struct Factory *_factory, struct Unit *_target_unit,
                 bool _low_power_attack, bool _defend);
    static inline RoleAttacker *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleAttacker))
                ? static_cast<struct RoleAttacker*>(role) : NULL); }

    static bool from_transition_low_power_attack(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_defend_territory(struct Role **new_role, struct Unit *_unit,
                                                 int max_count = 0);

    static struct Cell *_cutoff_cell(struct Unit *opp_cell);

    void print(std::ostream& os) const;
    struct RoleAttacker *copy() { return new RoleAttacker(*this); }
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
} RoleAttacker;
