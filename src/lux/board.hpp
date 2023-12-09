#pragma once

#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/factory.hpp"
#include "lux/json.hpp"
#include "lux/team.hpp"
#include "lux/unit.hpp"

#include <climits>
#include <functional>  // function
#include <string>
#include <vector>


struct Player;

typedef struct Board {
    int step;
    int sim_step;
    int real_env_step;
    Team home;
    Team away;
    Cell cells[SIZE2];
    std::vector<Factory> factories;
    std::vector<Unit> units;
    struct Player *player;
    struct Player *opp;

    int iceland_count;
    std::vector<Cell*> ice_cells;
    std::vector<Cell*> ore_cells;

    std::vector<std::pair<struct Cell*, int> > heavy_mine_cell_steps;
    std::vector<std::pair<struct Cell*, int> > light_mine_cell_steps;

    std::vector<std::pair<struct Cell*, int> > future_heavy_mine_cell_steps;
    std::vector<std::pair<struct Cell*, int> > future_light_mine_cell_steps;

    std::vector<std::pair<struct Cell*, int> > heavy_cow_cell_steps;
    std::vector<std::pair<struct Cell*, int> > light_cow_cell_steps;

    std::vector<std::pair<struct Cell*, int> > future_heavy_cow_cell_steps;
    std::vector<std::pair<struct Cell*, int> > future_light_cow_cell_steps;

    std::map<struct Unit*, std::vector<struct Cell*>*> opp_chains;

    int _factories_per_team;
    int _ice_vuln_count;
    int _flood_fill_call_id;
    int _pathfind_call_id;
    int _save_units_len;

    // ~~~ Methods:

    void init(json &obs, int agent_step, bool is_player0);
    std::string summary();
    void save_begin();
    void save_end();
    void load();

    inline bool sim0() { return this->step == this->sim_step; }  // is it currently sim step index 0?
    inline bool final_night() { return this->sim_step >= FINAL_NIGHT_PHASE; }

    Cell *cell(int x, int y);
    Cell *cell(int cell_id);
    Player *get_player(int player_id);

    void begin_step_simulation();
    void end_step_simulation();
    void update_roles_and_goals();

    void update_flatlands();
    void update_lowlands();
    void update_icelands();
    void update_disconnected_lichen();
    void update_future_mines();
    void update_opp_chains();

    bool low_iceland();

    // Every cell gets a score - maybe just once per step
    // For each factory
    //   for each lowland route
    //   for each resource route
    //   for each lichen frontier cell
    //   for each lichen damaged cell
    //   for each lichen bottleneck cell
    // Also RoleCow::from_cow_score
    //   iterate cells based on dist from factory or unit
    //   filtered by cell assignment, chain mine activity, proximity to opp factory, etc
    //   modified by distance from unit/factory
    // Overall: less control of order of digging, but uniquely interesting cells will get done early
    //
    // void update_cow_scores();

    void flood_fill(Cell *src, std::function<bool(Cell*)> const& cell_cond);

    int naive_cost(Unit *unit, Cell *src, Cell *dest_cell);
    int pathfind(Unit *unit, Cell *src, Cell *dest_cell,
                 std::function<bool(Cell*)> const& dest_cond = NULL,
                 std::function<bool(Cell*)> const& avoid_cond = NULL,
                 std::function<int(Cell*,Unit*)> const& custom_cost = NULL,
                 std::vector<Cell*> *route = NULL,
                 int max_dist = INT_MAX,
                 std::vector<Cell*> *src_cells = NULL);
    int pathfind(Cell *src, Cell *dest_cell,
                 std::function<bool(Cell*)> const& dest_cond = NULL,
                 std::function<bool(Cell*)> const& avoid_cond = NULL,
                 std::function<int(Cell*,Unit*)> const& custom_cost = NULL,
                 std::vector<Cell*> *route = NULL,
                 int max_dist = INT_MAX);
} Board;
extern Board board;
extern bool g_prod;
