#pragma once

#include <climits>
#include <cstddef>  // NULL
#include <iostream>
#include <vector>


struct Cell;
struct Factory;
struct Unit;

typedef struct Role {
    struct Unit *unit;
    char goal_type;  // 'c'ell, 'f'actory, 'u'nit
    void *goal;

    int _set_step;

    // ~~~ Methods:

    Role(struct Unit *_unit, char _goal_type = 'x');
    virtual ~Role() = default;

    bool is_set();
    bool _goal_is_factory();
    static void _displace_unit(struct Unit *unit);
    static void _displace_unit(struct Cell *cell);
    static void _displace_units(std::vector<struct Cell*> &cells);

    bool _do_move(struct Cell *goal_cell = NULL, bool allow_no_move = false);
    bool _do_move_direct(struct Cell *_goal_cell);
    bool _do_move_attack_trapped_unit();
    bool _do_no_move();
    bool _do_transfer_resource_to_factory(struct Cell *tx_cell_override = NULL,
                                          struct Unit *tx_unit_override = NULL);
    bool _do_power_pickup(int max_amount = INT_MAX, bool goal_is_factory_override = false,
                          struct Factory *alt_factory = NULL);
    bool _do_move_998();
    bool _do_move_999();
    bool _do_dig_999();
    bool _do_move_to_exploding_factory();
    bool _do_pickup_resource_from_exploding_factory();
    bool _do_move_win_collision();

    virtual void print(std::ostream& os) const = 0;
    virtual struct Role *copy() = 0;
    virtual struct Factory *get_factory() = 0;
    virtual double power_usage() = 0;
    virtual void set();
    virtual void unset();
    virtual void teardown() = 0;
    virtual bool is_valid() = 0;
    virtual struct Cell *goal_cell() = 0;
    virtual void update_goal() = 0;
    virtual bool do_move() = 0;
    virtual bool do_dig() = 0;
    virtual bool do_transfer() = 0;
    virtual bool do_pickup() = 0;

    friend std::ostream &operator<<(std::ostream &os, const struct Role &r) {
        r.print(os); return os;
    }
} Role;
