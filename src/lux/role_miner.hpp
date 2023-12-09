#pragma once

#include "lux/action.hpp"
#include "lux/log.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleMiner : Role {
    struct Factory *factory;
    struct Cell *resource_cell;

    // Optional chain transporter attribute(s)
    std::vector<struct Cell*> chain_route;
    std::vector<struct Unit*> chain_units;

    // Optional power transporter attribute(s)
    struct Unit *power_transporter;

    // Optional protector attribute(s)
    struct Unit *protector;

    int _power_ok_steps;
    int _power_ok_steps_step;

    // ~~~ Methods:

    RoleMiner(struct Unit *_unit, struct Factory *_factory, struct Cell *_resource_cell,
              std::vector<struct Cell*> *chain_route = NULL);
    static inline RoleMiner *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleMiner))
                ? static_cast<struct RoleMiner*>(role) : NULL); }

    static bool from_resource(struct Role **new_role, struct Unit *_unit, Resource resource,
                              int max_dist, int max_chain_dist, int max_count = 0);

    static bool from_transition_to_uncontested_ice(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_to_closer_ice(struct Role **new_role, struct Unit *_unit);
    static bool from_transition_to_ore(struct Role **new_role, struct Unit *_unit,
                                       int max_dist, int max_chain_dist);

    static bool role_is_similar(struct Role *r, Resource resource);
    static bool get_chain_route(struct Unit *unit, struct Cell *resource_cell, int max_dist,
                                std::vector<struct Cell*> *route);
    static int power_ok_steps(struct Unit *unit, struct Factory *factory);
    static int ore_digs(struct Factory *factory);
    static double resource_cell_score(struct Unit *unit, struct Cell *cell, int max_dist,
                                      std::vector<struct Cell*> *route = NULL);
    static bool factory_needs_water(struct Factory *factory, int steps, struct Unit *skip_unit = NULL);

    bool _do_excess_power_transfer();

    bool _is_patient();
    bool _ore_chain_is_paused();
    bool _transporters_exist(int dist_threshold = -1);

    void set_power_transporter(struct Unit *_unit);
    void unset_power_transporter();

    void set_chain_transporter(struct Unit *_unit, int chain_idx);
    void unset_chain_transporter(int chain_idx);

    void set_protector(struct Unit *_unit);
    void unset_protector();

    double water_income();

    // Standard Role methods
    void print(std::ostream& os) const;
    struct RoleMiner *copy() { return new RoleMiner(*this); }
    inline struct Factory *get_factory() { return this->factory; }
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
} RoleMiner;
