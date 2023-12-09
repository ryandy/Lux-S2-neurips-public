#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleProtector : Role {
    struct Cell *factory_cell;
    struct Unit *miner_unit;
    int last_strike_step;

    bool _is_protecting;
    int _is_protecting_step;

    bool _should_strike;
    int _should_strike_step;

    // ~~~ Methods:

    RoleProtector(struct Unit *_unit, struct Cell *_factory_cell, struct Unit *_miner_unit);
    static inline RoleProtector *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleProtector))
                ? static_cast<struct RoleProtector*>(role) : NULL); }

    static bool from_transition_protect_ice_miner(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_power_transporter(struct Role **new_role, struct Unit *_unit);

    static bool threat_units(struct Unit *miner_unit, int past_steps = 3, int max_radius = 3,
                             std::vector<Unit*> *threat_units = NULL);

    int threat_power(int past_steps = 3, int max_radius = 3);
    bool in_position();
    bool is_protecting();
    bool should_strike();
    bool is_striking();
    bool is_covering();

    void print(std::ostream& os) const;
    struct RoleProtector *copy() { return new RoleProtector(*this); }
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
} RoleProtector;
