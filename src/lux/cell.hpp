#pragma once

#include "lux/action.hpp"
#include "lux/defs.hpp"

#include <cmath>  // abs
#include <iostream>
#include <stdint.h>
#include <vector>


struct Cell;
struct Factory;
struct Player;
struct Unit;

typedef struct CellPathInfo {
    int cost;  // stores best cost per cell during each pathfind call
    int dist;  // stores dist associated with best cost route
    struct Cell *prev_cell;  // so we can unwind routes
    int call_id;  // so we can skip per-call all-cell initialization
} CellPathInfo;

typedef struct Cell {
    int16_t id;
    int16_t x;
    int16_t y;
    bool ice;
    bool ore;

    int8_t rubble;
    int8_t lichen;
    int8_t lichen_strain;

    bool valid_spawn;
    bool ice1_spawn;  // If a factory was placed here, there would be a dist-1 ice
    bool factory_center;
    struct Factory *factory;

    int16_t home_dist;  // man_dist to nearest "home team" factory
    int16_t away_dist;  // man_dist to nearest "away team" factory
    struct Factory *nearest_home_factory;
    struct Factory *nearest_away_factory;

    int flood_fill_call_id;
    CellPathInfo path_info;

    std::vector<Cell*> neighbors;
    std::vector<Cell*> neighbors_plus;  // includes self
    Cell *north;
    Cell *east;
    Cell *south;
    Cell *west;

    struct Unit *unit;  // unit located here now
    struct Unit *unit_next;  // unit located here next simulated step
    struct Unit *_unit_history[100];  // recent history of units located here

    struct Unit *assigned_unit;  // unit assigned to this cell
    struct Factory *assigned_factory;  // factory assigned to this (resource) cell

    int16_t flatland_id;
    int16_t flatland_size;
    int16_t lowland_id;
    int16_t lowland_size;
    int16_t iceland_id;
    int16_t iceland_size;

    double step0_score;

    bool _ice_vulnerable_cells_ready;
    std::vector<struct Cell*> _ice_vulnerable_cells;

    bool _is_contested;
    int _is_contested_step;

    int future_heavy_dig_step;  // Next time opp plans to dig here with heavy
    int future_light_dig_step;  // Next time opp plans to dig here with light
    int lichen_connected_step;
    int lichen_opp_boundary_step;  // Cell has opp lichen and blocks own lichen at this step
    int lichen_frontier_step;  // Cell has lichen and is adjacent to flatland
    int lichen_bottleneck_step;  // Can cut off outer cells at this step
    int lichen_bottleneck_cell_count;
    int lichen_bottleneck_lichen_count;
    int lichen_dist;  // Dist of cell from factory via lichen

    int8_t _save_rubble;
    int8_t _save_lichen;
    int8_t _save_lichen_strain;
    struct Factory *_save_assigned_factory;

    // ~~~ Methods:

    void init(int16_t cell_id, int16_t x, int16_t y, bool ice, bool ore, int8_t rubble);
    void init_neighbors();
    void reinit_rubble(int8_t rubble);
    void reinit_lichen(int8_t lichen);
    void reinit_lichen_strain(int8_t lichen_strain);
    void save_begin();
    void save_end();
    void load();

    struct Factory *own_factory(struct Player *player = NULL);
    struct Factory *opp_factory(struct Player *player = NULL);

    void update_unit_history(struct Unit *unit);
    struct Unit *get_unit_history(int step, struct Player *player = NULL);
    struct Unit *own_unit(struct Player *player = NULL);
    struct Unit *opp_unit(struct Player *player = NULL);

    bool is_surrounded();  // cell is currently surrounded by own units
    bool is_contested();  // cell is readily accessible by both teams

    Cell *neighbor(int dx, int dy);
    Cell *neighbor(Direction direction);
    Cell *neighbor_toward(Cell *other_cell);
    Direction neighbor_to_direction(Cell *neighbor);
    bool is_between(Cell *a, Cell *b);

    Cell *radius_cell(int max_radius, Cell *prev_cell = NULL);
    Cell *radius_cell(int min_radius, int max_radius, Cell *prev_cell = NULL);
    Cell *radius_cell_factory(int max_radius, Cell *prev_cell = NULL);
    Cell *radius_cell_factory(int min_radius, int max_radius, Cell *prev_cell = NULL);

    // const modifier needed for stable_sort for some reason
    inline int man_dist(Cell *other) const{ return abs(this->x - other->x) + abs(this->y - other->y); }
    int man_dist_factory(struct Factory *factory) const;
    int man_dist_factory(Cell *other) const;
    struct Factory *_nearest_factory(struct Player *player = NULL,
                                     struct Factory *ignore_factory = NULL);  // actually calculate
    struct Factory *nearest_factory(struct Player *player = NULL);  // use cached value
    int nearest_factory_dist(struct Player *player = NULL);
    void update_factory_dists();

    void set_unit_assignment(struct Unit *unit);
    void unset_unit_assignment(struct Unit *unit);

    double get_antagonize_score(bool heavy);
    PDD get_traffic_score(struct Player *player = NULL, bool include_neighbors = false);
    double get_spawn_score(double ore_mult = 1, bool verbose = false);
    double get_spawn_security_score();

    bool ice_vulnerable();
    bool ice_vulnerable(struct Cell *other_factory_cell);
    std::vector<struct Cell*> &ice_vulnerable_cells();

    friend std::ostream &operator<<(std::ostream &os, const Cell &c) {
	os << "(" << c.x << "," << c.y << ")"; return os;
    }
} Cell;
