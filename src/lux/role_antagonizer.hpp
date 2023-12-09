#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleAntagonizer : Role {
    struct Factory *factory;
    struct Cell *target_cell;

    struct Factory *target_factory;  // optional: target_cell becomes dynamic based on target_factory
    struct Unit *chain_miner;  // optional: miner unit associated with the chain cell being antagonized

    bool _can_destroy_factory_cache;
    int _can_destroy_factory_step;

    // ~~~ Methods:

    RoleAntagonizer(struct Unit *_unit, struct Factory *_factory, struct Cell *_target_cell,
                    struct Factory *_target_factory = NULL,
                    struct Unit *_chain_miner = NULL,
                    bool skip_factory = false);
    static inline RoleAntagonizer *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleAntagonizer))
                ? static_cast<struct RoleAntagonizer*>(role) : NULL); }

    static bool from_mine(struct Role **new_role, struct Unit *_unit, Resource resource,
                          int max_dist, int max_count = 0, int max_water = 0);
    static bool from_chain(struct Role **new_role, struct Unit *_unit,
                           int max_dist, int max_count = 0);
    static bool from_factory(struct Role **new_role, struct Unit *unit,struct Factory *target_factory);

    static bool from_transition_antagonizer_with_target_factory(struct Role **new_role,
                                                                struct Unit *_unit);
    static bool from_transition_destroy_factory(struct Role **new_role, struct Unit *_unit,
                                                int max_dist);

    static struct Cell *_get_factory_target_cell(struct Unit *unit, struct Factory *factory,
                                                 struct Cell *prev_target_cell = NULL);

    static bool _can_destroy_factory(struct Unit *unit, struct Cell *target_cell,
                                     struct Factory *target_factory, struct Unit *chain_miner,
                                     int power_cushion = 0);
    bool can_destroy_factory();

    void print(std::ostream& os) const;
    struct RoleAntagonizer *copy() { return new RoleAntagonizer(*this); }
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
} RoleAntagonizer;
