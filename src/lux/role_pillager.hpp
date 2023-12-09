#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RolePillager : Role {
    struct Factory *factory;
    struct Cell *lichen_cell;

    // ~~~ Methods:

    RolePillager(struct Unit *_unit, struct Factory *_factory, struct Cell *_lichen_cell);
    static inline RolePillager *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RolePillager))
                ? static_cast<struct RolePillager*>(role) : NULL); }

    static bool from_lichen(struct Role **new_role, struct Unit *_unit,
                            int max_dist, int max_count = 0, bool _bn = false);
    static bool from_lichen_bottleneck(struct Role **new_role, struct Unit *_unit,
                                       int max_dist, int max_count = 0);

    static bool from_transition_active_pillager(struct Role **new_role, struct Unit *_unit,
                                                int max_dist);
    static bool from_transition_end_of_game(struct Role **new_role, struct Unit *_unit,
                                            bool allow_pillager = false);

    static double _cell_score(struct Unit *unit, struct Cell *cell);

    void print(std::ostream& os) const;
    struct RolePillager *copy() { return new RolePillager(*this); }
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
} RolePillager;
