#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "lux/json.hpp"


struct Cell;
struct Factory;
struct Team;
struct Unit;

typedef struct Player {
    int8_t id;
    uint32_t strains;
    struct Team *team;

    int _units_step;
    std::list<struct Unit*> _units;
    int _factories_step;
    std::list<struct Factory*> _factories;

    std::vector<struct Cell*> lichen_disconnected_cells;

    // ~~~ Methods:

    void init(struct Team *team, bool is_player0, json &strains_info);
    std::list<struct Unit*> &units();
    std::list<struct Factory*> &factories();
    void add_new_units();
    inline bool is_strain(int strain_id) { return (strain_id >= 0
                                                   && (this->strains & (1 << strain_id))); }
    void get_new_actions(json *actions);
} Player;
