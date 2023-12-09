#pragma once

#include "lux/action.hpp"
#include "lux/defs.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleBlockade : Role {
    struct Factory *factory;
    struct Unit *target_unit;
    struct Factory *target_factory;
    struct Unit *partner;

    struct Factory *last_transporter_factory;  // where most recent transporter originated
    int last_transporter_step;  // last step where a valid transporter existed - used for anticipation

    int avoid_step;  // most recent step that unit selected goal cell in order to avoid a threat
    int push_step;  // most recent step that unit selected goal cell to push target away from factory
    int next_swap_and_idle_step;

    bool straightline;
    Direction force_direction;
    PII force_direction_step;

    bool _is_primary;
    int _is_primary_step;

    bool _is_engaged;
    int _is_engaged_step;

    struct Cell *_goal_cell;
    int _goal_cell_step;

    std::vector<struct Cell*> _target_route;
    int _target_route_step;

    std::vector<struct Cell*> _unengaged_goal_cell_candidates;
    int _unengaged_goal_cell_candidates_step;

    // ~~~ Methods:

    RoleBlockade(struct Unit *_unit, struct Factory *_factory, struct Unit *_target_unit,
                 struct Factory *_target_factory, struct Unit *_partner);
    static inline RoleBlockade *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleBlockade))
                ? static_cast<struct RoleBlockade*>(role) : NULL); }

    static bool from_transition_block_water_transporter(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_block_different_water_transporter(struct Role **new_role,
                                                                  struct Unit *_unit);

    static bool is_between(struct Cell *mid_cell,
                           struct Cell *cell1, struct Cell *cell2,
                           bool neighbors = false);
    static bool is_between(struct Cell *mid_cell1, struct Cell *mid_cell2,
                           struct Cell *cell1, struct Cell *cell2,
                           bool neighbors = false);

    struct Cell *opp_cell();
    bool has_partner();
    bool has_target_unit();
    bool is_primary();
    bool is_engaged();
    bool is_ready_but_low_power();

    double unengaged_goal_cell_score(struct Cell *cell);
    std::vector<struct Cell*> &target_route();
    std::vector<struct Cell*> &unengaged_goal_cell_candidates();
    struct Cell *unengaged_goal_cell();

    bool set_goal_cell_primary_intercept();
    bool set_goal_cell_secondary_rendezvous();
    bool set_goal_cell_primary_engaged();
    bool set_goal_cell_secondary_engaged();
    bool set_goal_cell_chill();

    void print(std::ostream& os) const;
    struct RoleBlockade *copy() { return new RoleBlockade(*this); }
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
} RoleBlockade;
