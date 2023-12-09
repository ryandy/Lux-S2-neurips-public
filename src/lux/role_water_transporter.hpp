#pragma once

#include "lux/action.hpp"
#include "lux/role.hpp"


struct Cell;
struct Factory;
struct Unit;

typedef struct RoleWaterTransporter : Role {
    struct Factory *factory;
    struct Factory *target_factory;

    // ~~~ Methods:

    RoleWaterTransporter(struct Unit *_unit, struct Factory *_factory,struct Factory *_target_factory);
    static inline RoleWaterTransporter *cast(struct Role *role) {
        return ((role && typeid(*role) == typeid(struct RoleWaterTransporter))
                ? static_cast<struct RoleWaterTransporter*>(role) : NULL); }

    static bool from_ice_conflict(struct Role **new_role, struct Unit *_unit,
                                  int water_threshold = 130, int max_count = 2);

    static bool from_transition_ice_conflict(struct Role **new_role, struct Unit *_unit,
                                             int water_threshold = 125, int max_count = 2);

    bool _do_water_pickup();

    void print(std::ostream& os) const;
    struct RoleWaterTransporter *copy() { return new RoleWaterTransporter(*this); }
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
} RoleWaterTransporter;
