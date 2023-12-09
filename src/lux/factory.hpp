#pragma once

#include "lux/action.hpp"
#include "lux/defs.hpp"

#include <cstdint>
#include <functional>  // function
#include <iostream>
#include <list>
#include <string>
#include <vector>


struct Cell;
struct Mode;
struct Player;
struct Role;
struct Unit;

typedef struct Factory {
    int16_t id;
    struct Player *player;
    int16_t x;
    int16_t y;
    int16_t ice;
    int16_t ore;
    int16_t water;
    int16_t metal;
    int32_t power;

    int16_t ice_delta;
    int16_t ore_delta;
    int16_t water_delta;
    int16_t metal_delta;
    int32_t power_delta;

    struct Cell *cell;
    int16_t alive_step;

    struct Mode *mode;
    struct Mode *_save_mode;

    std::vector<struct Cell*> cells;  // does not include center cell
    std::vector<struct Cell*> cells_plus;  // includes center cell
    std::list<struct Unit*> units;
    std::list<struct Unit*> heavies;
    std::list<struct Unit*> lights;

    std::vector<struct Cell*> ice_cells;  // sorted by dist
    std::vector<struct Cell*> ore_cells;  // sorted by dist

    std::vector<std::vector<struct Cell*>*> ice_routes;
    std::vector<std::vector<struct Cell*>*> ore_routes;
    //std::vector<std::vector<struct Cell*>*> flatland_routes;
    std::vector<std::vector<struct Cell*>*> lowland_routes;

    int lichen_connected_count;  // measured at the beginning of the step only
    std::vector<struct Cell*> lichen_connected_cells;  // cells with lichen
    std::vector<struct Cell*> lichen_growth_cells;  // connected + this step's new lichen cells
    std::vector<struct Cell*> lichen_flat_boundary_cells;  // adjacent to connected w/ no rubble
    std::vector<struct Cell*> lichen_rubble_boundary_cells;  // adjacent to connected w/ rubble
    std::vector<struct Cell*> lichen_frontier_cells;  // connected, adjacent to flatland
    std::vector<struct Cell*> lichen_bottleneck_cells;  // connected, can cut off outer cells

    std::vector<std::pair<struct Cell*, int> > pillage_cell_steps;  // where/when lichen was attacked

    // Convenience counts - NOTE: cannot be referenced until after Role::is_valid checks
    int heavy_ice_miner_count;
    int heavy_ore_miner_count;
    int heavy_antagonizer_count;
    int light_relocate_count;
    int heavy_relocate_count;
    int inbound_light_relocate_count;
    int inbound_heavy_relocate_count;
    int inbound_water_transporter_count;

    bool _ice_vuln_covered;  // used by agent_setup()

    int last_action_step;
    FactoryAction action;  // temporary holder for next action before maybe being moved to new_action
    FactoryAction new_action;

    // ~~~ Methods:

    void init(int factory_id, int player_id, int x, int y, int water, int metal);
    void reinit(int ice, int ore, int water, int metal, int power);
    void save_end();
    void load();
    void handle_destruction();
    bool alive();

    std::string id_str();

    int total_water(int extra_ice = 0) {
        return (this->water + this->water_delta
                + (this->ice + this->ice_delta + extra_ice) / ICE_WATER_RATIO); }
    int total_metal(int extra_ore = 0) {
        return (this->metal + this->metal_delta
                + (this->ore + this->ore_delta + extra_ore) / ORE_METAL_RATIO); }
    int total_power() { return this->power + this->power_delta - this->power_reserved(); }

    struct Cell *cell_toward(struct Cell *other_cell);
    struct Cell *neighbor_toward(struct Cell *other_cell);
    struct Cell *radius_cell(int max_radius, struct Cell *prev_cell = NULL);
    struct Cell *radius_cell(int min_radius, int max_radius, struct Cell *prev_cell = NULL);

    void new_mode(struct Mode *new_mode);
    void delete_mode();

    int power_reserved();
    double power_usage(struct Unit *skip_unit = NULL);
    int power_gain();
    double water_income(struct Unit *skip_unit = NULL);
    void update_lichen_info(bool is_begin_step = false);
    void update_lichen_bottleneck_info();

    void update_lowland_routes(int max_dist = 8, int min_lowland_size = 6);
    void update_resource_routes(Resource resource, int max_dist, int max_count);

    void update_units();
    void add_unit(struct Unit *unit);
    void remove_unit(struct Unit *unit);

    int get_similar_unit_count(struct Unit *unit, std::function<bool(Role*)> const& similar_cond);

    bool _can_build_safely();
    bool can_build_light();
    void do_build_light();
    bool can_build_heavy();
    void do_build_heavy();

    int water_cost();
    bool can_water();
    void do_water();

    void do_none();

    friend std::ostream &operator<<(std::ostream &os, const struct Factory &f);
} Factory;
