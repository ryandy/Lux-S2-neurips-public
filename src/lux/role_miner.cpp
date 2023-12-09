#include "lux/role_miner.hpp"

#include <algorithm>  // reverse
#include <string>
#include <vector>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_chain_transporter.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_defender.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleMiner::RoleMiner(Unit *_unit, Factory *_factory, Cell *_resource_cell, vector<Cell*> *chain_route)
    : Role(_unit, 'f'), factory(_factory), resource_cell(_resource_cell)
{
    this->goal = _factory;
    this->_power_ok_steps_step = -1;

    this->chain_route.clear();
    this->chain_units.clear();
    if (chain_route) {
        this->chain_route = *chain_route;
        this->chain_units.resize(chain_route->size(), NULL);
    }

    this->power_transporter = NULL;
    this->protector = NULL;
}

bool RoleMiner::from_resource(Role **new_role, Unit *_unit, Resource resource,
                              int max_dist, int max_chain_dist, int max_count) {
    if (resource == Resource_ORE
        && board.sim_step >= END_PHASE - 15) return false;
    //if (_unit->_log_cond()) LUX_LOG("RoleMiner::from_resource A " << max_dist);

    Factory *factory = _unit->assigned_factory;

    // Check for max miners of this type/size
    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return role_is_similar(r, resource); }) >= max_count) return false;

    // Check for sufficient power before mining ore
    if (_unit->heavy
        && resource == Resource_ORE
        && RoleMiner::power_ok_steps(_unit, factory) < MINER_SAFE_POWER_STEPS) return false;

    // Verify factory has enough water to survive ore mining mission
    if (_unit->heavy
        && resource == Resource_ORE
        && factory->heavy_ice_miner_count == 0) {
        int factory_water = factory->total_water(_unit->ice + _unit->ice_delta);
        int water_threshold = (25  // ore digs
                               + 6
                               + 25);
        if (factory->cell->away_dist < 20) water_threshold += 45;
        if (factory_water < water_threshold) return false;
    }

    // Don't start mining ice if we don't need water
    if (resource == Resource_ICE
        && !RoleMiner::factory_needs_water(factory, 175)) return false;

    // Extend max_chain_dist for isolated factories
    if (_unit->heavy && max_chain_dist && factory->cell->away_dist >= 35) {
        max_chain_dist = 3 * max_chain_dist / 2;
    }

    Cell *best_cell = NULL;
    double best_score = INT_MIN / 2;
    vector<Cell*> route;
    vector<Cell*> best_route;
    vector<Cell*> &cells = (resource == Resource_ICE) ? factory->ice_cells : factory->ore_cells;
    for (Cell *cell : cells) {
        int dist = cell->man_dist_factory(factory);
        if (dist > MAX(max_dist, max_chain_dist)) break;

        if (!cell->assigned_unit
            || (_unit->heavy && !cell->assigned_unit->heavy)  // lighter unit
            || (_unit->heavy  // mining chain
                && cell->assigned_unit->heavy
                && RoleMiner::cast(cell->assigned_unit->role)
                && cell != RoleMiner::cast(cell->assigned_unit->role)->resource_cell)
            || (cell->ice  // miner from unassigned factory
                && _unit->heavy == cell->assigned_unit->heavy
                && cell->assigned_factory == _unit->assigned_factory
                && cell->assigned_factory != cell->assigned_unit->assigned_factory)) {

            vector<Cell*> *route_ptr = NULL;
            if (_unit->heavy
                && dist <= max_chain_dist
                && RoleMiner::get_chain_route(_unit, cell, max_chain_dist, &route)
                && route.size() > 1) {
                route_ptr = &route;
            }

            double score = INT_MIN;
            if (dist <= max_dist) {
                score = RoleMiner::resource_cell_score(
                    _unit, cell, max_dist, NULL);
            }
            if (route_ptr) {
                double chain_score = RoleMiner::resource_cell_score(
                    _unit, cell, max_chain_dist, route_ptr);
                if (chain_score > score) {
                    score = chain_score;
                } else {
                    route_ptr = NULL;
                }
            }

            //if (board.sim0() && _unit->id == 48 && cell->ice) {
            //    LUX_LOG("RM::from_resource " << *cell << ' ' << (bool)route_ptr << ' ' << score);
            //}

            if (score > best_score) {
                best_score = score;
                best_cell = cell;
                if (route_ptr) best_route = *route_ptr;
                else best_route.clear();
            }
        }
    }

    if (best_cell) {
        if (!best_route.empty()) Role::_displace_units(best_route);
        Role::_displace_unit(best_cell);
        *new_role = new RoleMiner(_unit, factory, best_cell, &best_route);
        return true;
    }

    return false;
}

bool RoleMiner::from_transition_to_uncontested_ice(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    LUX_ASSERT(_unit->heavy);
    //if (_unit->_log_cond()) LUX_LOG("RoleMiner::from_transition_to_uncontested_ice A");

    RoleMiner *role;
    if (!board.sim0()
        || !_unit->antagonizer_unit
        || !(role = RoleMiner::cast(_unit->role))
        || !role->resource_cell->ice
        || role->protector
        || !role->resource_cell->is_contested()) {
        return false;
    }

    Factory *factory = _unit->assigned_factory;
    for (Cell *ice_cell : factory->ice_cells) {
        int ice_cell_dist = ice_cell->man_dist_factory(factory);
        if (ice_cell_dist > 8) break;
        if (ice_cell_dist < ice_cell->away_dist
            && (ice_cell->assigned_factory == NULL
                || ice_cell->assigned_factory == factory)
            && (!ice_cell->assigned_unit
                || !ice_cell->assigned_unit->heavy)
            && !ice_cell->is_contested()) {

            // assigned_unit checks don't work when this is called from a Mode transition function
            bool cell_assigned = false;
            for (Unit *other_unit : board.player->units()) {
                if (other_unit == _unit || !other_unit->heavy) continue;
                if (RoleMiner::cast(other_unit->role)
                    && RoleMiner::cast(other_unit->role)->resource_cell == ice_cell) {
                    cell_assigned = true; break;
                }
                if (RoleAntagonizer::cast(other_unit->role)
                    && RoleAntagonizer::cast(other_unit->role)->target_cell == ice_cell) {
                    cell_assigned = true; break;
                }
            }
            if (cell_assigned) continue;

            // Assume too crazy to attempt a chain
            LUX_LOG(*_unit << " change ice cell from " << *role->resource_cell
                    << " to uncontested " << *ice_cell);
            Role::_displace_unit(ice_cell);
            *new_role = new RoleMiner(_unit, factory, ice_cell);
            return true;
        }
    }

    return false;
}

bool RoleMiner::from_transition_to_closer_ice(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    LUX_ASSERT(_unit->heavy);
    //if (_unit->_log_cond()) LUX_LOG("RoleMiner::from_transition_to_closer_ice A");

    RoleMiner *role;
    Factory *factory = _unit->assigned_factory;
    if (!(role = RoleMiner::cast(_unit->role))
        || !role->resource_cell->ice
        || role->resource_cell->man_dist_factory(factory) == 1) {
        return false;
    }

    Cell *best_cell = NULL;
    int min_dist = INT_MAX;
    bool cur_contested = role->resource_cell->is_contested();
    for (Cell *ice_cell : factory->ice_cells) {
        int dist = ice_cell->man_dist_factory(factory);
        if (dist > 1) break;  // Only consider adjacent ice cells

        if (ice_cell->is_contested()) {
            if (!cur_contested) continue;  // Don't transition into contested situation
            dist += 1;  // Penalize contested cells
        }

        if (dist < min_dist
            && (!ice_cell->assigned_unit
                || !ice_cell->assigned_unit->heavy
                || (ice_cell->assigned_factory == factory
                    && ice_cell->assigned_unit->assigned_factory != factory))) {
            min_dist = dist;
            best_cell = ice_cell;
        }
    }

    if (best_cell) {
        if (board.sim0()) {
            LUX_LOG(*_unit << " change ice cell from " << *role->resource_cell
                    << " to closer " << *best_cell);
        }
        Role::_displace_unit(best_cell);
        *new_role = new RoleMiner(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RoleMiner::from_transition_to_ore(Role **new_role, Unit *_unit, int max_dist,int max_chain_dist) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    LUX_ASSERT(_unit->heavy);
    //if (_unit->_log_cond()) LUX_LOG("RoleMiner::from_transition_to_ore A");

    Factory *factory = _unit->assigned_factory;
    if (board.sim_step < 25
        || board.sim_step >= END_PHASE - 15
        || factory->heavy_ore_miner_count > 0) return false;

    // Some roles/situations should not transition to ore mining
    auto unit_is_exempt = [&](Unit *u) {
        return (u->role
                && (false
                    || (RoleAntagonizer::cast(_unit->role)
                        && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
                    || (RoleAttacker::cast(_unit->role)
                        && RoleAttacker::cast(_unit->role)->low_power_attack)
                    || (RoleCow::cast(_unit->role)
                        && RoleCow::cast(_unit->role)->repair)
                    || (RoleMiner::cast(u->role)
                        && (RoleMiner::cast(u->role)->resource_cell->ore))
                    || RolePincer::cast(u->role)
                    || RoleProtector::cast(u->role)
                    || RoleRecharge::cast(u->role)
                    || RoleRelocate::cast(u->role)
                    ));
    };
    if (unit_is_exempt(_unit)) return false;

    // Don't distract ice conflict heavies
    if (ModeIceConflict::cast(factory->mode)
        && (!_unit->role
            || RoleAntagonizer::cast(_unit->role)
            || RoleMiner::cast(_unit->role)
            || RoleRecharge::cast(_unit->role))) return false;

    // Assume unit can use some of factory's power
    int unit_power = _unit->power + _unit->power_delta;
    int factory_power = factory->total_power();
    if (factory->heavies.size() == 1) unit_power += factory_power;
    else unit_power += factory_power / 2;

    // Verify ore is needed
    // TODO force heavy ore if best ore cell is far
    int ore_digs = RoleMiner::ore_digs(factory);
    if (ore_digs <= 0) return false;

    // Power check #1
    int power_threshold = 3 * _unit->cfg->MOVE_COST + ore_digs * _unit->cfg->DIG_COST;
    if (unit_power < power_threshold) return false;

    // Find target ore cell
    Cell *ore_cell = NULL;
    double best_score = INT_MIN;
    for (Cell *c : factory->ore_cells) {
        // Can displace lights and paused heavy miners
        if (c->assigned_unit && c->assigned_unit->heavy) {
            RoleMiner *other_role_miner = RoleMiner::cast(c->assigned_unit->role);
            if (!other_role_miner || !other_role_miner->_ore_chain_is_paused()) continue;
        }
        int dist = c->man_dist_factory(factory);
        if (dist > max_dist) break;

        // TODO: get chain route here?
        double score = RoleMiner::resource_cell_score(_unit, c, max_dist);
        if (score > best_score) {
            ore_cell = c;
            best_score = score;
        }
    }
    if (!ore_cell) return false;

    // Get chain route if possible
    int ore_cell_dist = ore_cell->man_dist_factory(factory);
    vector<Cell*> chain_route;
    vector<Cell*> *chain_route_ptr = NULL;
    if (ore_cell_dist > 1 && ore_cell_dist <= max_chain_dist) {
        if (RoleMiner::get_chain_route(_unit, ore_cell, max_chain_dist, &chain_route)
            && (int)chain_route.size() <= max_chain_dist) {
            chain_route_ptr = &chain_route;
        }
    }

    // Power check #2
    int rubble_digs = 0;
    int power_gain = 0;
    int extra_power = 0;
    if (ore_cell_dist > 1 && !chain_route_ptr) {
        rubble_digs = ((ore_cell->rubble + _unit->cfg->DIG_RUBBLE_REMOVED - 1)
                           / _unit->cfg->DIG_RUBBLE_REMOVED);
        int total_steps = 2 * ore_cell_dist + rubble_digs + ore_digs;
        power_gain = _unit->power_gain(board.sim_step, board.sim_step + total_steps);
        extra_power = (
            _unit->cfg->ACTION_QUEUE_POWER_COST
            + _unit->cfg->DIG_COST
            + 2 * _unit->cfg->MOVE_COST * ore_cell_dist);
        power_threshold = (
            2 * _unit->cfg->ACTION_QUEUE_POWER_COST
            + 2 * _unit->cfg->MOVE_COST * ore_cell_dist
            + (rubble_digs + ore_digs) * _unit->cfg->DIG_COST
            + extra_power
            - power_gain);
        if (unit_power < power_threshold) return false;
    }

    // Verify factory has enough water to survive ore mining mission
    if (factory->heavy_ice_miner_count == 0
        || (factory->heavy_ice_miner_count == 1
            && RoleMiner::cast(_unit->role))) {
        int factory_water = factory->total_water(_unit->ice + _unit->ice_delta);
        int water_threshold = (2 * ore_cell_dist
                               + ore_digs
                               + 6
                               + 25);
        if (factory->cell->away_dist < 20) water_threshold += 45;
        if (factory_water < water_threshold) return false;
    }

    // Only transition ice miner if no other good choices
    if (RoleMiner::cast(_unit->role)) {
        bool all_high_priority = true;
        for (Unit *u : factory->heavies) {
            if (!(u->role
                  && (RoleMiner::cast(u->role)
                      || unit_is_exempt(u)))) {
                all_high_priority = false;
                break;
            }
        }
        if (!all_high_priority) return false;
    }

    // Verify factory has enough power to last a while
    if (RoleMiner::power_ok_steps(_unit, factory) < MINER_SAFE_POWER_STEPS) return false;

    // Power check #3
    if (ore_cell_dist > 1 && !chain_route_ptr) {
        int cost_to = board.pathfind(_unit, _unit->cell(), ore_cell);
        int cost_from = board.pathfind(_unit, ore_cell, factory->cell);
        if (cost_to == INT_MAX || cost_from == INT_MAX) return false;
        power_threshold = (
            2 * _unit->cfg->ACTION_QUEUE_POWER_COST
            + cost_to + cost_from
            + (rubble_digs + ore_digs) * _unit->cfg->DIG_COST
            + extra_power
            - power_gain);
        if (unit_power < power_threshold) return false;
    }

    // Success!
    if (chain_route_ptr) Role::_displace_units(chain_route);
    Role::_displace_unit(ore_cell);
    *new_role = new RoleMiner(_unit, factory, ore_cell, chain_route_ptr);
    return true;
}

bool RoleMiner::role_is_similar(Role *r, Resource resource) {
    RoleMiner *rm = RoleMiner::cast(r);
    return rm && rm->resource_cell->ice == (resource == Resource_ICE);
}

bool RoleMiner::get_chain_route(Unit *unit, Cell *resource_cell, int max_dist, vector<Cell*> *route) {
    LUX_ASSERT(resource_cell);
    LUX_ASSERT(route);

    // Limit chain length if limited lights are available
    int available_lights = unit->assigned_factory->total_metal() / g_light_cfg.METAL_COST;
    for (Unit *u : unit->assigned_factory->lights) {
        if (!RoleRelocate::cast(u->role)
            && !RoleChainTransporter::cast(u->role)
            && !RolePowerTransporter::cast(u->role)
            && !RoleWaterTransporter::cast(u->role)) available_lights++;
    }
    if (available_lights < 2) return false;
    max_dist = MIN(max_dist, available_lights);

    int cost = board.pathfind(
        unit, resource_cell, unit->assigned_factory->cell, NULL,
        // Avoid factories and heavy non-defenders
        [&](Cell *c) { return (c->factory
                               || (c->assigned_unit
                                   && c->assigned_unit != unit
                                   && c->assigned_unit->heavy
                                   && !RoleDefender::cast(c->assigned_unit->role))); },
        [&](Cell *c, Unit *u) { (void)u;
            return (100
                    - MIN(15, c->away_dist)  // want high away_dist
                    + int(1 <= c->rubble && c->rubble <= 19)  // want flat/hi rubble
                    + 200 * int(c->ice || c->ore));  // want non-resource
        },
        route,
        max_dist);
    if (cost == INT_MAX) return false;

    reverse(route->begin(), route->end());  // pathfind goes from resource to factory
    route->pop_back();  // Last route cell is for miner
    return true;
}

int RoleMiner::power_ok_steps(Unit *unit, Factory *factory) {
    int current_step = 1000 * board.step + board.sim_step;
    RoleMiner *role_miner = RoleMiner::cast(unit->role);

    // Return cached result if possible
    if (role_miner
        && current_step == role_miner->_power_ok_steps_step) return role_miner->_power_ok_steps;

    // Ignore power usage of existing heavy ore miners
    double power_usage = factory->power_usage(/*skip_unit*/unit);
    if (!(role_miner
          && role_miner->resource_cell->ore)) power_usage += (0.8 * 60 + 0.1 * 20 - 6);

    // TODO: incorporate unit's power in some cases?
    int power_gain = factory->power_gain();
    int power = factory->power + factory->power_delta; // + unit->power + unit->power_delta;
    int steps_remaining = 1000;
    if (power_usage > power_gain) steps_remaining = power / (power_usage - power_gain);

    // Cache results in role object if it exists
    if (role_miner) {
        //if (board.sim0()) LUX_LOG("power ok " << *unit << ' ' << power << ' '
        //                          << power_usage << ' ' << power_gain << ' ' << steps_remaining);
        role_miner->_power_ok_steps = steps_remaining;
        role_miner->_power_ok_steps_step = current_step;
    }

    return steps_remaining;
}

int RoleMiner::ore_digs(Factory *factory) {
    int factory_ore = (factory->ore + factory->ore_delta
                       + (factory->metal + factory->metal_delta) * ORE_METAL_RATIO);
    int factory_metal = factory->total_metal();

    int future_lights = factory_metal / g_light_cfg.METAL_COST;
    int extra_ore = (future_lights
                     * g_light_cfg.METAL_COST
                     * ORE_METAL_RATIO
                     + (factory_ore % ORE_METAL_RATIO));
    int ore_needed = 0;
    if (factory->mode->build_heavy_next()) ore_needed = 500 - extra_ore;
    else ore_needed = 200 - extra_ore;

    if (ore_needed <= 0) return 0;
    return (ore_needed + g_heavy_cfg.DIG_RESOURCE_GAIN - 1) / g_heavy_cfg.DIG_RESOURCE_GAIN;
}

double RoleMiner::resource_cell_score(Unit *unit, Cell *cell, int max_dist, vector<Cell*> *route) {
    LUX_ASSERT(unit);
    LUX_ASSERT(cell);
    LUX_ASSERT(cell->ice || cell->ore);
    LUX_ASSERT(!route || route->size() >= 2);

    Factory *factory = unit->assigned_factory;
    if (cell->man_dist_factory(factory) > max_dist) return INT_MIN;

    // Get cost for chain / probable commute
    int cost = INT_MAX, dist = INT_MAX;
    if (route) {
        dist = route->size();
        cost = dist * unit->cfg->MOVE_COST;
    } else {
        vector<Cell*> commute_route;
        cost = board.pathfind(
            unit, cell, factory->cell,
            [&](Cell *c) {
                if (c->factory == factory) {
                    if (c->assigned_unit) {
                        if (unit->heavy && c->man_dist(cell) == 1) {
                            // Ok if this is an adj heavy miner displacing a light CT
                            if (RoleMiner::cast(c->assigned_unit->role)) {
                                return true;
                            }
                            // Ok if this is an adj heavy ice miner displacing a light ore PT
                            RolePowerTransporter *role_pt;
                            if (cell->ice
                                && (role_pt = RolePowerTransporter::cast(c->assigned_unit->role))
                                && !role_pt->unit->heavy
                                && RoleMiner::cast(role_pt->target_unit->role)
                                && RoleMiner::cast(role_pt->target_unit->role)->resource_cell->ore) {
                                return true;
                            }
                        }
                    } else {  // on factory, no assigned unit
                        return true;
                    }
                }
                return false;
            },
            [&](Cell *c) {
                // Avoid factories and non-defenders
                return (c->factory
                        || (c->assigned_unit
                            && c->assigned_unit != unit
                            && !RoleDefender::cast(c->assigned_unit->role))); },
            NULL, &commute_route, max_dist);

        // Bad route: return min score
        if (cost == INT_MAX) {
            return INT_MIN;
        } else {
            dist = commute_route.size() - 1;
        }
    }
    LUX_ASSERT(dist > 0);

    // Score is roughly equal to negative dist
    double score = INT_MIN;
    if (route) {
        // chain routes can be almost as efficient as dist1 resources under ideal conditions
        score = -1 - 0.2 * dist;
    } else if (unit->heavy) {
        // route will likely be cow'd eventually, so mostly consider dist
        score = -1 * (1.8 * dist + 0.2 * cost / (double)unit->cfg->MOVE_COST) / 2.0;
    } else {
        score = -1 * (1.0 * dist + 1.0 * cost / (double)unit->cfg->MOVE_COST) / 2.0;
    }
    //if (board.sim0() && unit->id == 48 && cell->ice) LUX_LOG("RM::scoreA " << *cell << ' ' << score);

    // TODO: maybe a box_out bonus (own factory is between resource and its nearest opp factory)
    // TODO: subtract more for chains near opp dist / traffic

    // Avoid opp-traffic'd cells
    PDD traffic_score = cell->get_traffic_score(board.opp, /*neighbors*/true);
    score -= 3 * traffic_score.first;
    if (route || !unit->heavy) score -= 3 * traffic_score.second;
    //if (board.sim0() && unit->id == 48 && cell->ice) LUX_LOG("RM::scoreB " << *cell << ' ' << score);

    // Avoid contested cells as a rough tie-breaker
    if (cell->is_contested()) score -= 0.25;
    //if (board.sim0() && unit->id == 48 && cell->ice) LUX_LOG("RM::scoreC " << *cell << ' ' << score);

    // Want low cost/dist, high opp dist, really don't want opp_dist=1
    int opp_dist = cell->away_dist;
    int opp_dist_near = MIN(4, opp_dist);
    int opp_dist_far = MIN(15, opp_dist);
    if      (dist == 1)     score +=  7 + opp_dist_near + 0.1 * opp_dist_far;
    else if (opp_dist == 1) score += -4;
    else                    score += opp_dist_near + 0.1 * opp_dist_far;
    //if (board.sim0() && unit->id == 48 && cell->ice) LUX_LOG("RM::scoreD " << *cell << ' ' << score);

    // Try not to chain-mine an ore cell that is convenient for another own factory
    if (cell->ore) {  // TODO: && route?
        double teamwork_penalty = 0;
        for (Factory *f : factory->player->factories()) {
            if (f != factory && cell == f->ore_cells[0]) {
                int d0 = f->ore_cells[0]->man_dist_factory(f);
                if (d0 <= 20) {
                    int d1 = f->ore_cells[1]->man_dist_factory(f);
                    teamwork_penalty = MIN(10, (d1 - d0)) / 2.0;
                    break;
                }
            }
        }
        score -= teamwork_penalty;
    }

    return score;
}

bool RoleMiner::factory_needs_water(Factory *factory, int steps, Unit *skip_unit) {
    LUX_ASSERT(factory);
    if (board.sim_step + steps > 1000) steps -= (board.sim_step + steps - 1000) / 2;
    int factory_water = factory->total_water();
    if (factory_water >= 300) {
        int water_income_without_unit = factory->water_income(skip_unit);
        int water_profit = water_income_without_unit - factory->water_cost() - 1;
        if (factory_water + steps * water_profit > 0) return false;
    }
    return true;
}

bool RoleMiner::_is_patient() {
    if (!this->unit->heavy || this->unit->cell() != this->resource_cell) return false;

    if (!this->power_transporter && this->chain_route.empty()) return false;

    // Check that standing still is safe
    if (board.sim0() && this->unit->move_risk(this->unit->cell())) return false;

    return ((this->protector && this->_transporters_exist(/*dist*/0))
            || (this->power_transporter && this->_transporters_exist(/*dist*/0))
            || (!this->chain_route.empty() && this->_transporters_exist(/*dist*/1)));
}

bool RoleMiner::_ore_chain_is_paused() {
    int light_lim = 14 + board.sim_step / 100;
    int light_count = this->factory->lights.size();

    bool is_paused = (this->unit->heavy
                      && this->resource_cell->ore
                      && light_count < light_lim - 2
                      && (this->power_transporter || !this->chain_route.empty())
                      && RoleMiner::power_ok_steps(this->unit, this->factory) < MINER_LOW_POWER_STEPS);
    return is_paused;
}

bool RoleMiner::_transporters_exist(int dist_threshold) {
    if (this->protector) {
        RoleProtector *role = RoleProtector::cast(this->protector->role);
        if (role
            && (dist_threshold < 0
                || this->protector->cell()->man_dist(role->factory_cell) <= dist_threshold)) {
            return true;
        }
    }
    else if (this->power_transporter) {
        RolePowerTransporter *role = RolePowerTransporter::cast(this->power_transporter->role);
        if (role
            && (dist_threshold < 0
                || this->power_transporter->cell()->man_dist(role->factory_cell) <= dist_threshold)) {
            return true;
        }
    } else if (!this->chain_route.empty()) {
        for (Unit *u : this->chain_units) {
            if (!u) return false;
            RoleChainTransporter *role = RoleChainTransporter::cast(u->role);
            if (!(role
                  && (dist_threshold < 0
                      || u->cell()->man_dist(role->target_cell) <= dist_threshold))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void RoleMiner::set_power_transporter(Unit *_unit) {
    this->power_transporter = _unit;
}

void RoleMiner::unset_power_transporter() {
    this->power_transporter = NULL;
}

void RoleMiner::set_chain_transporter(Unit *_unit, int chain_idx) {
    LUX_ASSERT(chain_idx < (int)this->chain_units.size());
    this->chain_units[chain_idx] = _unit;
}

void RoleMiner::unset_chain_transporter(int chain_idx) {
    LUX_ASSERT(chain_idx < (int)this->chain_units.size());
    this->chain_units[chain_idx] = NULL;
}

void RoleMiner::set_protector(Unit *_unit) {
    this->protector = _unit;
}

void RoleMiner::unset_protector() {
    this->protector = NULL;
}

void RoleMiner::print(ostream &os) const {
    string r = this->resource_cell->ice ? "Ice" : "Ore";
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string rgoal = this->goal_type == 'c' ? "*" : "";
    string mod = " ";
    if (this->protector) mod = "pr ";
    else if (this->power_transporter) mod = "pt ";
    else if (!this->chain_route.empty()) mod = "ct ";
    os << r << "Miner[" << *this->factory << fgoal << " ->" << mod
       << *this->resource_cell << rgoal << "]";
}

double RoleMiner::power_usage() {
    double power_usage = 0;
    if (!this->chain_route.empty()) {
        power_usage = 5 * this->unit->cfg->DIG_COST / 6.0;  // 5 digs and a transfer
    } else if (this->power_transporter) {
        power_usage = 20 * this->unit->cfg->DIG_COST / 21.0;  // this can vary - 25 digs is half cargo
    } else if (this->protector) {
        // assuming 1 strike per 3 digs (i.e. 3:2 dig-to-move ratio)
        power_usage = (40 * this->unit->cfg->ACTION_QUEUE_POWER_COST
                       + 24 * this->unit->cfg->DIG_COST
                       + 16 * this->unit->cfg->MOVE_COST) / 41.0;
    } else {
        int dist = this->resource_cell->man_dist_factory(this->factory);
        power_usage = ((25 * this->unit->cfg->DIG_COST
                        + 2 * dist * this->unit->cfg->MOVE_COST)
                       / (25 + 2 * dist + 2));
    }
    return power_usage;
}

double RoleMiner::water_income() {
    if (!this->resource_cell->ice) return 0;
    if (this->unit->antagonizer_unit) return 0;

    if (!this->chain_route.empty()) {
        return g_light_cfg.CARGO_SPACE / ICE_WATER_RATIO / 6.0;  // 5 digs and a transfer
    }

    if (this->power_transporter) {
        int digs = 25;  // Note: can vary
        return digs * (double)this->unit->cfg->DIG_RESOURCE_GAIN / ICE_WATER_RATIO / (digs + 1.0);
    }

    if (this->protector) {
        int digs = 24;
        return digs * (double)this->unit->cfg->DIG_RESOURCE_GAIN / ICE_WATER_RATIO / (digs + 16 + 1.0);
    }

    int move_dist = this->resource_cell->man_dist_factory(this->factory);
    double rubble_estimate = 1.25;
    double move_cost = (2 * rubble_estimate * move_dist
                        * (this->unit->cfg->MOVE_COST - 0.6 * this->unit->cfg->CHARGE));
    int digs = 6;
    if (this->factory->power >= 5000) {
        double digs_by_power = ((0.9 * this->unit->cfg->BATTERY_CAPACITY - move_cost)
                            / (this->unit->cfg->DIG_COST - 0.6 * this->unit->cfg->CHARGE));
        double digs_by_cargo = 0.75* this->unit->cfg->CARGO_SPACE / this->unit->cfg->DIG_RESOURCE_GAIN;
        digs = MIN(digs_by_power, digs_by_cargo);
    }
    int ice_cargo = digs * this->unit->cfg->DIG_RESOURCE_GAIN;
    int period = 2 * move_dist + digs + 2;
    double wi = ice_cargo / (double)ICE_WATER_RATIO / period;
    //LUX_LOG(" " << *this->unit << " " << move_dist << ' ' << digs << ' ' << wi);
    return wi;
}

void RoleMiner::set() {
    Role::set();
    this->resource_cell->set_unit_assignment(this->unit);
    for (Cell *cell : this->chain_route) cell->set_unit_assignment(this->unit);
    if (this->unit->heavy && this->resource_cell->ice) this->factory->heavy_ice_miner_count++;
    if (this->unit->heavy && this->resource_cell->ore) this->factory->heavy_ore_miner_count++;
}

void RoleMiner::unset() {
    if (this->is_set()) {
        this->resource_cell->unset_unit_assignment(this->unit);
        for (Cell *cell : this->chain_route) cell->unset_unit_assignment(this->unit);
        if (this->unit->heavy && this->resource_cell->ice) this->factory->heavy_ice_miner_count--;
        if (this->unit->heavy && this->resource_cell->ore) this->factory->heavy_ore_miner_count--;
        Role::unset();
    }
}

void RoleMiner::teardown() {
    // Delete role of power transporter
    if (this->power_transporter
        && RolePowerTransporter::cast(this->power_transporter->role)
        && RolePowerTransporter::cast(this->power_transporter->role)->target_unit == this->unit) {
        this->power_transporter->delete_role();
    }

    // Delete roles of chain transporters
    for (Unit *_unit : this->chain_units) {
        if (_unit
            && RoleChainTransporter::cast(_unit->role)
            && RoleChainTransporter::cast(_unit->role)->target_unit == this->unit) {
            _unit->delete_role();
        }
    }

    // Delete role of protector
    if (this->protector
        && RoleProtector::cast(this->protector->role)
        && RoleProtector::cast(this->protector->role)->miner_unit == this->unit) {
        this->protector->delete_role();
    }
}

bool RoleMiner::is_valid() {
    if (!this->factory->alive()) return false;

    // Stop mining ice if we have sufficient water
    if (this->resource_cell->ice
        && !RoleMiner::factory_needs_water(this->factory, 200, /*skip_unit*/this->unit)) return false;

    // Stop mining ore during the final game phase
    if (this->resource_cell->ore
        && board.sim_step >= END_PHASE + 20) return false;

    bool is_paused = this->_ore_chain_is_paused();

    // Stop mining ore if factory cannot support more units, and there is no heavy ice miner
    // TODO: also check if existing ice miner(s) is antagonized
    if (this->unit->ore == 0
        && is_paused) {
        // Note: cannot use factory->heavy_ice_miner_count before is_valid checks are complete
        int heavy_ice_miner_count = 0;
        for (Unit *u : this->factory->heavies) {
            RoleMiner *role = RoleMiner::cast(u->role);
            if (role && role->resource_cell->ice) heavy_ice_miner_count++;
        }
        if (heavy_ice_miner_count == 0) {
            return false;
        }
    }

    // Stop mining ore if this is factory's only heavy and factory is low on water
    // Note: cannot use factory->heavy_relocate_count etc during is_valid
    int heavy_ice_miner_count = 0;
    int other_heavy_nearby_count = 0;
    for (Unit *heavy_unit : this->factory->heavies) {
        if (heavy_unit == this->unit) continue;
        if (RoleMiner::cast(heavy_unit->role)
            && RoleMiner::cast(heavy_unit->role)->resource_cell->ice) {
            heavy_ice_miner_count++;
        }
        else if (!RoleRelocate::cast(heavy_unit->role)
                 && heavy_unit->cell()->man_dist_factory(this->factory) < 10) {
            other_heavy_nearby_count++;
        }
    }
    //if (this->unit->_log_cond()) LUX_LOG(*this->unit << " is_valid_A " << heavy_ice_miner_count
    //                                     << ' ' << other_heavy_nearby_count);
    if (this->unit->heavy
        && this->resource_cell->ore
        && heavy_ice_miner_count == 0
        && other_heavy_nearby_count == 0) {
        int total_water = this->factory->total_water();
        int ice_dist = this->factory->ice_cells[0]->man_dist_factory(this->factory);
        int move_dist = (this->unit->cell()->man_dist(this->resource_cell)
                         + this->resource_cell->man_dist_factory(this->factory)
                         + 4
                         + 2 * ice_dist);

        int total_ore = this->unit->ore;
        for (Unit *chain_unit : this->factory->lights) {
            // Cannot use this->chain_units during is_valid()
            if (RoleChainTransporter::cast(chain_unit->role)
                && RoleChainTransporter::cast(chain_unit->role)->target_unit == this->unit) {
                total_ore += chain_unit->ore;
            }
        }
        int total_metal = this->factory->total_metal(/*extra_ore*/total_ore);

        //if (this->unit->_log_cond()) LUX_LOG(*this->unit << " is_valid_B " << total_water
        //                                     << ' ' << move_dist);
        if (total_water < move_dist + 15) {
            return false;
        }

        if (total_water < move_dist + 20
            && (total_metal < 80
                || total_metal >= 100
                || this->unit->cell() != this->resource_cell
                || is_paused)) {
            return false;
        }

        if (total_water < move_dist + 60
            && this->factory->cell->away_dist < 20
            && (total_metal < 60
                || total_metal >= 100
                || this->unit->cell() != this->resource_cell
                || is_paused)) {
            return false;
        }

        if (total_water < move_dist + 100
            && this->unit->antagonizer_unit
            && this->unit->cell()->man_dist(this->resource_cell) <= 1) {
            return false;
        }
    }

    return true;
}

Cell *RoleMiner::goal_cell() {
    // Override goal if on factory center
    Cell *cur_cell = this->unit->cell();
    if (cur_cell == this->factory->cell) return this->resource_cell;
    if (this->goal_type == 'c') return this->resource_cell;

    // Goal is factory
    if (this->unit->heavy && cur_cell->factory != this->factory) {
        Cell *fcell = this->factory->cell_toward(cur_cell);
        RolePowerTransporter *role;
        if (!fcell->assigned_unit  // no one
            || fcell->assigned_unit == this->unit  // own CT
            || ((role = RolePowerTransporter::cast(fcell->assigned_unit->role))  // own PT
                && role->target_unit == this->unit)) {
            return fcell;
        }
    }

    return this->factory->cell;
}

void RoleMiner::update_goal() {
    int unit_resource = MAX(this->unit->ice, this->unit->ore);
    int resource_dist = this->resource_cell->man_dist_factory(this->factory);
    Cell *cur_cell = this->unit->cell();

    if (this->_ore_chain_is_paused() && cur_cell == this->resource_cell) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal ->f (ore chain paused)");
        this->goal_type = 'f';
        this->goal = this->factory;
        return;
    }

    if (this->goal_type == 'c') {  // Done with resource cell goal?
        // Handled by RoleRecharge if: no transporters, > 5 dist
        int power_threshold = 0;
        if (this->protector
            && this->_transporters_exist(/*dist*/0)) {
            power_threshold = (this->unit->cfg->ACTION_QUEUE_POWER_COST
                               + 3 * this->unit->cfg->MOVE_COST);
        } else if (this->_transporters_exist(/*dist*/1)) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal A");
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * this->unit->cfg->MOVE_COST  // Necessary? Consistent with low_power baseline
                + board.naive_cost(this->unit, cur_cell, this->resource_cell)
                + this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, this->resource_cell, this->factory->cell));
        } else if (resource_dist <= 5) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal B");
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * this->unit->cfg->MOVE_COST  // Necessary? Consistent with low_power baseline
                + board.naive_cost(this->unit, cur_cell, this->resource_cell)
                + this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, this->resource_cell, this->factory->cell));
        }
        power_threshold = MIN(power_threshold, 0.5 * this->unit->cfg->BATTERY_CAPACITY);
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal c->f " << power_threshold);
        int resource_threshold = 3 * this->unit->cfg->CARGO_SPACE / 4;
        if (this->unit->power < power_threshold || unit_resource >= resource_threshold) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Return to factory early if low power and threatened by opp
        else if (board.sim0()
                 && this->unit->low_power
                 && this->unit->move_risk(cur_cell) > 0) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Deliver ore to factory early if we now have materials to build a heavy
        else if (this->unit->ore
                 && this->resource_cell->ore
                 && this->chain_route.empty()
                 && this->factory->total_metal() < g_heavy_cfg.METAL_COST
                 && this->factory->total_metal(this->unit->ore) >= g_heavy_cfg.METAL_COST) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Deliver ice to factory early if factory is low on water
        else if (this->unit->ice
                 && this->factory->total_water() < 10 + cur_cell->man_dist_factory(this->factory)) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Deliver ice to factory early if we have ice and game is almost over
        else if (board.sim_step + resource_dist >= ICE_RUSH_PHASE
                 && this->unit->ice >= 4 * this->unit->cfg->DIG_RESOURCE_GAIN) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Deliver ice to factory early if we are an early-game solo heavy ice miner
        else if (board.sim_step < 200
                 && board.sim_step % 3 == 0
                 && this->resource_cell->ice
                 && this->unit->ice
                 && this->unit->heavy
                 && this->factory->heavies.size() == 1
                 && this->_transporters_exist()
                 && !ModeIceConflict::cast(this->factory->mode)
                 && this->factory->total_power() < 2000
                 && this->factory->total_water() > 250) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

        // Deliver ice to factory early if factory is too low on water to water lichen
        else if (this->resource_cell->ice
                 && this->unit->ice >= 100
                 && this->unit->heavy
                 && (resource_dist == 1 || this->_transporters_exist(/*dist*/0))
                 && !ModeIceConflict::cast(this->factory->mode)
                 && this->factory->total_water() < 55) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }

    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = INT_MAX;
        // Note: this can cause chain miners to continue digging when they are very low power
        //if (this->_is_patient()) {
        //    if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal C");
        //    power_threshold = 0.03 * this->unit->cfg->BATTERY_CAPACITY;
        //} else
        if (this->_transporters_exist(/*dist*/1)) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal D");
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * this->unit->cfg->MOVE_COST  // Necessary? Consistent with low_power baseline
                + board.naive_cost(this->unit, cur_cell, this->resource_cell)
                + 2 * this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, this->resource_cell, this->factory->cell));
        } else if (this->factory->power >= 5000) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal E");
            power_threshold = 0.95 * this->unit->cfg->BATTERY_CAPACITY;
        } else {
            //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal F");
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * this->unit->cfg->MOVE_COST  // Necessary? Consistent with low_power baseline
                + board.naive_cost(this->unit, cur_cell, this->resource_cell)
                + 6 * this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, this->resource_cell, this->factory->cell));
        }
        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::update_goal f->c " << power_threshold);
        if (this->unit->power >= power_threshold && unit_resource == 0) {
            this->goal_type = 'c';
            this->goal = this->resource_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleMiner::do_move() {
    // Don't move if we can just wait on our transporter(s)
    if (this->_is_patient()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_move A (is_patient)");
        return this->_do_no_move();
    }

    // Try not to move if in position and chain is paused
    if (this->_ore_chain_is_paused() && this->unit->cell() == this->resource_cell) {
        return false;
    }

    // Normal move
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_move B (normal move)");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_move C");
    return false;
}

bool RoleMiner::do_dig() {
    if (this->goal_type == 'c' && this->unit->cell() == this->resource_cell) {
        if (this->_ore_chain_is_paused()) {
            return false;
        }
        if (this->protector
            && this->_transporters_exist(/*dist*/0)
            && this->unit->power < (this->unit->cfg->ACTION_QUEUE_POWER_COST
                                    + 3 * this->unit->cfg->MOVE_COST
                                    + 1 * this->unit->cfg->DIG_COST)) {
            return false;
        }
        // Throttle ice mining power usage if there's an ore miner?
        //if (board.sim_step >= 200
        //    && board.sim_step < 800
        //    && board.sim_step % 4 == 0
        //    && this->factory->heavy_ore_miner_count > 0
        //    && this->factory->power < 2 * this->factory->power_gain()
        //    && this->_transporters_exist()
        //    && this->factory->total_water(/*extra_ice*/this->unit->ice) >= 80) {
        //    return false;
        //}
        if (this->unit->power >= this->unit->dig_cost()) {
            this->unit->do_dig();
            return true;
        }
    }
    return false;
}

bool RoleMiner::_do_excess_power_transfer() {
    // Transfer excess power to factory if we have a power transporter / protector
    // Make sure we can do this transfer even if out of position/goal if w/ protector needing power
    Cell *cur_cell = this->unit->cell();
    if (this->unit->heavy
        && (this->power_transporter || this->protector)
        && (cur_cell == this->resource_cell || this->protector)
        && this->unit->power >= 1500
        && this->factory->power < 500
        && !ModeIceConflict::cast(this->factory->mode)
        && cur_cell->man_dist_factory(this->factory) <= 1) {
        Cell *tx_cell = cur_cell->neighbor_toward(this->factory->cell);
        int amount = this->unit->power - 600;
        amount = (amount / 10) * 10;  // round down to nearest 10
        if (amount > 0
            && this->unit->power >= this->unit->transfer_cost(tx_cell, Resource_POWER, amount)) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *this->unit->role
                                      << " miner excess power tx " << amount << ' '
                                      << (cur_cell == this->resource_cell));
            this->unit->do_transfer(tx_cell, Resource_POWER, amount);
            return true;
        }
    }

    return false;
}

bool RoleMiner::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_transfer A");
    if (this->_do_excess_power_transfer()) {
        return true;
    }

    // Transfer resources to factory-bound chain transporter (regardless of current goal)
    if (!this->chain_units.empty()
        && ((this->goal_type == 'f' && (this->unit->ice || this->unit->ore))
            || this->unit->ice >= g_light_cfg.CARGO_SPACE
            || this->unit->ore >= g_light_cfg.CARGO_SPACE)) {
        Unit *factory_bound_unit = this->chain_units[this->chain_units.size() - 1];
        Cell *factory_bound_cell = this->chain_route[this->chain_route.size() - 1];
        if (factory_bound_unit && this->unit->cell()->man_dist(factory_bound_cell) == 1) {
            Cell *cur_cell = factory_bound_unit->cell();
            Cell *next_cell = factory_bound_unit->cell_next();
            if (next_cell == factory_bound_cell  // moving to cell
                || (cur_cell == factory_bound_cell && next_cell == NULL)) {  // at cell, no move yet
                if (this->_do_transfer_resource_to_factory(factory_bound_cell, factory_bound_unit)) {
                    if (this->unit->_log_cond()) LUX_LOG(*this->unit << " miner tx to " << *factory_bound_unit);
                    return true;
                }
            }
        }
    }

    // Normal transfer to factory
    //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_transfer B");
    return this->_do_transfer_resource_to_factory();
}

bool RoleMiner::do_pickup() {
    if (this->_do_power_pickup()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_pickup A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleMiner::do_pickup B");
    return false;
}
