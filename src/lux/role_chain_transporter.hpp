#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleChainTransporter : Role {
    struct Factory *factory;
    struct Cell *target_cell;
    struct Unit *target_unit;
    int chain_idx;

    // ~~~ Methods:

    RoleChainTransporter(struct Unit *_unit, struct Factory *_factory, struct Cell *_target_cell,
                         struct Unit *_target_unit, int _chain_idx);
    static inline RoleChainTransporter *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleChainTransporter))
                ? static_cast<struct RoleChainTransporter*>(role) : NULL); }

    static bool from_miner(struct Role **new_role, struct Unit *_unit, int max_dist = 0);
    static bool from_transition_partial_chain(struct Role **new_role, struct Unit *unit, int max_dist);

    bool do_move_last_chain();
    bool do_move_if_threatened(bool last_chain_only);
    bool do_no_move_if_receiving();

    struct Cell *_get_factory_bound_cell(struct Unit **factory_bound_unit_out);
    struct Cell *_get_miner_bound_cell(struct Unit **miner_bound_unit_out);

    void print(std::ostream& os) const;
    struct RoleChainTransporter *copy() { return new RoleChainTransporter(*this); }
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
} RoleChainTransporter;
