#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleCow : Role {
    struct Factory *factory;
    struct Cell *rubble_cell;

    bool repair;

    // ~~~ Methods:

    RoleCow(struct Unit *_unit, struct Factory *_factory, struct Cell *_rubble_cell,
            bool _repair = false);
    static inline RoleCow *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleCow))
                ? static_cast<struct RoleCow*>(role) : NULL); }

    static bool from_lowland_route(struct Role **new_role, struct Unit *_unit,
                                   int max_dist, int min_size, int max_count = 0);
    static bool from_resource_route(struct Role **new_role, struct Unit *_unit, Resource resource,
                                    int max_dist, int max_routes, int max_count = 0);
    static bool from_lichen_frontier(struct Role **new_role, struct Unit *_unit,
                                     int max_dist, int max_rubble = 100, int max_connected = 10000);
    static bool from_lichen_bottleneck(struct Role **new_role, struct Unit *_unit,
                                       int max_dist, int min_rubble = 1);
    static bool from_custom_route(struct Role **new_role, struct Unit *_unit,
                                  struct Cell *target_cell, int max_count = 0);
    static bool from_lichen_repair(struct Role **new_role, struct Unit *_unit,
                                   int max_dist, int max_count = 0);

    static bool from_transition_lichen_repair(struct Role **new_role, struct Unit *_unit,
                                              int max_count = 0);

    void print(std::ostream& os) const;
    struct RoleCow *copy() { return new RoleCow(*this); }
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
} RoleCow;
