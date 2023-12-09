#include "agent.hpp"

#include <algorithm>  // stable_sort
#include <utility>  // pair

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/log.hpp"
using namespace std;

/*
  Python features:
  ---------------
  ice1_bonus
  ore1_bonus
  forge_bonus
  iso_bonus
  iso_counter_bonus
  ice_conflict_bonus
  -ice_security_strong_bonus
  ice_security_weak_bonus
  secure_other_factory_bonus
  desperate_ice_conflict_bonus
  ~
  -ice1_dist
  -ice2_dist
  -ore1_dist
  -ore1_dist_long
  low_weighted_capped
  low_weighted_full
  ore_weighted
  desperate_ice_dist

  C++ features:
  ------------
  ice_security_strong_bonus
  ~
  ice1_dist
  ice2_dist
  ore1_dist
  ore1_dist_long
  flat_dist

  New features:
  ------------
  oasis_bonus - isolated ice deposit, some lowland
  oasis_with_ore_bonus - isolated ice deposit, nearby ore, some lowland
  nearest_spawnable_ice_dist - related to oasis
*/


json Agent::setup() {
    json actions = json::object();

    // Opponent's turn to place factory
    if (this->step != 0 && this->step % 2 != (int)this->place_first) {
	return actions;
    }

    // Update ice vuln count and ore mult
    board._ice_vuln_count = 0;
    double ore_mult = 1;
    if (board.step > 0) {
        for (Factory *opp_factory : board.opp->factories()) {
            opp_factory->_ice_vuln_covered = false;
            vector<Cell*> &ice_vuln_cells = opp_factory->cell->ice_vulnerable_cells();
            for (Factory *own_factory : board.player->factories()) {
                if (count(ice_vuln_cells.begin(), ice_vuln_cells.end(), own_factory->cell)) {
                    board._ice_vuln_count += 1;
                    opp_factory->_ice_vuln_covered = true;
                    //LUX_LOG(*opp_factory << " IC covered by " << *own_factory);
                    break;
                }
            }
        }

        int opp_ore_count = 0;
        int own_ore_count = 0;
        for (Factory *opp_factory : board.opp->factories()) {
            if (opp_factory->ore_cells[0]->man_dist_factory(opp_factory) <= 5) opp_ore_count++;
        }
        for (Factory *own_factory : board.player->factories()) {
            if (own_factory->ore_cells[0]->man_dist_factory(own_factory) <= 5) own_ore_count++;
        }
        if (opp_ore_count >= own_ore_count) {
            ore_mult = 2;
        }
    }

    // Score each possible spawn_cell
    vector<pair<Cell*, double> > cell_scores1;
    for (Cell &spawn_cell : board.cells) {
        if (spawn_cell.valid_spawn) {
            //if (board.step == 10
            //    && spawn_cell.x == 61
            //    && spawn_cell.y == 43) (void)spawn_cell.get_spawn_score(ore_mult, true);
            double score = spawn_cell.get_spawn_score(ore_mult);
            if (this->step == 0) spawn_cell.step0_score = score;
            cell_scores1.emplace_back(&spawn_cell, score);
        }
    }
    stable_sort(cell_scores1.begin(), cell_scores1.end(),
                [&](const pair<Cell*,double> &a, const pair<Cell*,double> &b) {
                    return a.second > b.second; });

    // Modify the most promising cells with an ice-security bonus
    vector<pair<Cell*, double> > cell_scores2;
    for (int i = 0; i < (int)cell_scores1.size(); i++) {
        Cell *spawn_cell = cell_scores1[i].first;
        double score_bonus = 0;
        if (i < 50) {
            score_bonus = spawn_cell->get_spawn_security_score();
        }
        cell_scores2.emplace_back(spawn_cell, cell_scores1[i].second + score_bonus);
    }
    stable_sort(cell_scores2.begin(), cell_scores2.end(),
                [&](const pair<Cell*,double> &a, const pair<Cell*,double> &b) {
                    return a.second > b.second; });

    // First step: bid for first factory placement
    if (this->step == 0) {
        // Low ice seeds: 109, 44570609, 517525016, 682050041, 917281543
	actions["bid"] = (board.low_iceland() ? 30 : 0);
	actions["faction"] = "AlphaStrike";
	return actions;
    }

    // Debug print:
    if (board.step <= 2) {
        for (int i = 0; i < 3; i++) {
            (void)cell_scores2[i].first->get_spawn_score(ore_mult, /*verbose*/true);
        }
    } else {
        (void)cell_scores2[0].first->get_spawn_score(ore_mult, /*verbose*/true);
    }

    Cell *best_cell = cell_scores2[0].first;
    actions["spawn"] = {best_cell->x, best_cell->y};

    int metal = this->metal_left / this->factories_left;
    if (metal % 10 != 0 && this->metal_left >= metal + 10 - (metal % 10)) metal += 10 - (metal % 10);
    int water = this->water_left / this->factories_left;
    if (water % 10 != 0 && this->water_left >= water + 10 - (water % 10)) water += 10 - (water % 10);

    int factory_num = (board.step + 1) / 2;
    if (board.low_iceland()) {
        if (factory_num <= 2) {
            water = MIN(this->water_left, 200);
        }
        if (factory_num == 1) {
            metal = MIN(this->metal_left, 250);
        }
    }

    actions["metal"] = metal;
    actions["water"] = water;

    this->metal_left -= metal;
    this->water_left -= water;
    this->factories_left -= 1;

    return actions;
}
