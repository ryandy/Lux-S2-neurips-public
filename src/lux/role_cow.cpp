#include "lux/role_cow.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleCow::RoleCow(Unit *_unit, Factory *_factory, Cell *_rubble_cell, bool _repair)
    : Role(_unit), factory(_factory), rubble_cell(_rubble_cell), repair(_repair)
{
    int fdist = _unit->cell()->man_dist_factory(_factory);
    int cdist = _unit->cell()->man_dist(_rubble_cell);
    if (fdist < cdist) {
        this->goal_type = 'f';
        this->goal = _factory;
    } else {
        this->goal_type = 'c';
        this->goal = _rubble_cell;
    }
}

bool RoleCow::from_lowland_route(Role **new_role, Unit *_unit,
                                 int max_dist, int min_size, int max_count) {
    //if (_unit->_log_cond()) LUX_LOG("RoleCow::from_lowland_route A");
    Factory *factory = _unit->assigned_factory;
    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RoleCow::cast(r); }) >= max_count) return false;

    for (vector<Cell*> *route : factory->lowland_routes) {
        if ((int)route->size() - 2 <= max_dist
            && route->back()->lowland_size >= min_size) {
            for (Cell *route_cell : *route) {
                if (!route_cell->assigned_unit
                    && route_cell->rubble > 0) {
                    *new_role = new RoleCow(_unit, factory, route_cell);
                    return true;
                }
            }
        }
    }

    return false;
}

bool RoleCow::from_resource_route(Role **new_role, Unit *_unit, Resource resource,
                                  int max_dist, int max_routes, int max_count) {
    //if (_unit->_log_cond()) LUX_LOG("RoleCow::from_resource_route A");
    Factory *factory = _unit->assigned_factory;
    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RoleCow::cast(r); }) >= max_count) return false;

    vector<vector<Cell*>*> &routes = (resource == Resource_ICE
                                      ? factory->ice_routes
                                      : factory->ore_routes);

    int route_count = 0;
    for (vector<Cell*> *route : routes) {
        if (route_count >= max_routes) break;

        Cell *rcell = route->back();
        LUX_ASSERT(rcell->ice || rcell->ore);

        if ((int)route->size() - 1 > max_dist) break;

        // Skip resources assigned to a different factory
        if (resource == Resource_ICE
            && rcell->assigned_factory
            && rcell->assigned_factory != factory) continue;

        route_count++;

        // Skip active chain mine routes
        RoleMiner *role_miner;
        if (rcell->assigned_unit
            && (role_miner = RoleMiner::cast(rcell->assigned_unit->role))
            && !role_miner->chain_route.empty()) continue;

        for (Cell *route_cell : *route) {
            if (!route_cell->assigned_unit
                && route_cell->rubble > 0
                && route_cell->away_dist >= 3) {
                *new_role = new RoleCow(_unit, factory, route_cell);
                return true;
            }
        }
    }

    return false;
}

bool RoleCow::from_lichen_frontier(Role **new_role, Unit *_unit,
                                   int max_dist, int max_rubble, int max_connected) {
    //if (_unit->_log_cond()) LUX_LOG("RoleCow::from_lichen_frontier A");
    Factory *factory = _unit->assigned_factory;
    if ((int)factory->lichen_connected_cells.size() > max_connected) return false;

    // If there are/will be 10+ flat boundary cells, exit
    int flat_boundary_count = factory->lichen_flat_boundary_cells.size();
    for (Cell *c : factory->lichen_rubble_boundary_cells) {
        if (c->assigned_unit && RoleCow::cast(c->assigned_unit->role)) flat_boundary_count++;
    }
    if (flat_boundary_count >= 10) return false;

    Cell *cur_cell = _unit->cell();
    Cell *best_cell = NULL;
    double min_cost = INT_MAX;
    for (Cell *c : factory->lichen_rubble_boundary_cells) {
        if (c->rubble > max_rubble
            || c->assigned_unit
            || c->man_dist_factory(factory) > max_dist) continue;
        int dist_unit_to_cell = cur_cell->man_dist(c);
        int dist_cell_to_factory = c->man_dist_factory(factory);
        int dist_cell_to_opp_factory = c->away_dist;
        double total_dist = (dist_unit_to_cell
                             + dist_cell_to_factory
                             - 0.25 * dist_cell_to_opp_factory);
        int dig_count = ((c->rubble + _unit->cfg->DIG_RUBBLE_REMOVED - 1)
                         / _unit->cfg->DIG_RUBBLE_REMOVED);
        double cost = (_unit->cfg->MOVE_COST * total_dist
                       + _unit->cfg->DIG_COST * dig_count);
        if (cost < min_cost) {
            best_cell = c;
            min_cost = cost;
        }
    }

    if (best_cell) {
        *new_role = new RoleCow(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RoleCow::from_lichen_bottleneck(Role **new_role, Unit *_unit,
                                     int max_dist, int min_rubble) {
    //if (_unit->_log_cond()) LUX_LOG("RoleCow::from_lichen_bottleneck A");
    Factory *factory = _unit->assigned_factory;

    Cell *cur_cell = _unit->cell();
    Cell *best_cell = NULL;
    double min_cost = INT_MAX;
    for (Cell *c : factory->lichen_bottleneck_cells) {
        if (c->lichen_bottleneck_cell_count <= 1) continue;
        for (Cell *neighbor : c->neighbors) {
            if (neighbor->rubble <= 0
                || neighbor->rubble < min_rubble
                || (neighbor->assigned_unit
                    && (neighbor->assigned_unit->heavy || !_unit->heavy))
                || neighbor->man_dist_factory(factory) > max_dist) continue;
            int dist_unit_to_cell = cur_cell->man_dist(c);
            int dist_cell_to_factory = c->man_dist_factory(factory);
            int dist_cell_to_opp_factory = c->away_dist;
            double total_dist = (dist_unit_to_cell
                                 + dist_cell_to_factory
                                 - 0.25 * dist_cell_to_opp_factory);
            int dig_count = ((c->rubble + _unit->cfg->DIG_RUBBLE_REMOVED - 1)
                             / _unit->cfg->DIG_RUBBLE_REMOVED);
            double cost = (_unit->cfg->MOVE_COST * total_dist
                           + _unit->cfg->DIG_COST * dig_count);
            if (cost < min_cost) {
                best_cell = neighbor;
                min_cost = cost;
            }
        }
    }
    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RoleCow(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RoleCow::from_custom_route(Role **new_role, Unit *_unit, Cell *target_cell, int max_count) {
    if (!target_cell) return false;

    Factory *factory = _unit->assigned_factory;
    if (max_count
        && (factory->get_similar_unit_count(_unit, [&](Role *r) { return RoleCow::cast(r); })
            >= max_count)) return false;

    vector<Cell*> route;
    board.pathfind(_unit, target_cell, factory->cell, NULL,
                   [&](Cell *c) { return c->factory; },
                   [&](Cell *c, Unit *u) { (void)u; return 20 + c->rubble; },
                   &route);

    Cell *best_cell = NULL;
    int min_dist = INT_MAX;
    for (Cell *cell : route) {
        int dist = cell->man_dist_factory(factory);
        if (dist < min_dist
            && cell->rubble > 0
            && !cell->assigned_unit
            && !_unit->threat_units(cell, /*steps*/2, /*radius*/1)) {
            min_dist = dist;
            best_cell = cell;
        }
    }

    if (best_cell) {
        *new_role = new RoleCow(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RoleCow::from_lichen_repair(Role **new_role, Unit *_unit, int max_dist, int max_count) {
    Factory *factory = _unit->assigned_factory;
    if (max_count
        && (factory->get_similar_unit_count(
                _unit, [&](Role *r) { return RoleCow::cast(r) && RoleCow::cast(r)->repair; })
            >= max_count)) return false;

    Cell *best_cell = NULL;
    int min_steps = INT_MAX;
    for (int i = factory->pillage_cell_steps.size() - 1; i >= 0; i--) {
        Cell *cell = factory->pillage_cell_steps[i].first;
        int step = factory->pillage_cell_steps[i].second;
        if (step < board.step - 50) break;

        if (cell->rubble > 0
            && cell->man_dist_factory(factory) <= max_dist
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))) {
            int steps = _unit->cell()->man_dist(cell) + cell->rubble / _unit->cfg->DIG_RUBBLE_REMOVED;
            if (steps < min_steps) {
                min_steps = steps;
                best_cell = cell;
            }
        }
    }

    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RoleCow(_unit, factory, best_cell, /*repair*/true);
        return true;
    }

    return false;
}

bool RoleCow::from_transition_lichen_repair(Role **new_role, Unit *_unit, int max_count) {
    //if (_unit->_log_cond()) LUX_LOG("RoleCow::from_transition_lichen_repair A");
    if (!board.sim0() || board.sim_step < 200) return false;

    Factory *factory = _unit->assigned_factory;

    // Various roles/situations should not transition
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack
                && RoleAttacker::cast(_unit->role)->target_unit->power < 200
                && _unit->heavy)
            || RoleBlockade::cast(_unit->role)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || (RoleMiner::cast(_unit->role)
                && RoleMiner::cast(_unit->role)->resource_cell->ore
                && _unit->heavy
                && _unit->ore)
            || RolePincer::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    // Be careful with low-water factories
    if ((factory->total_water() < 60
         || (factory->total_water() < 120 && factory->heavy_ice_miner_count <= 1))
        && _unit->heavy
        && (!_unit->role
            || (RoleMiner::cast(_unit->role)
                && RoleMiner::cast(_unit->role)->resource_cell->ice)
            || RoleProtector::cast(_unit->role))) return false;

    // Don't distract solo ice_conflict heavy
    if (ModeIceConflict::cast(factory->mode)
        && _unit->heavy
        && factory->heavies.size() == 1) return false;

    // Must be nearby
    Cell *cur_cell = _unit->cell();
    if (cur_cell->man_dist_factory(factory) > 8) return false;

    // Max count
    if (max_count
        && (factory->get_similar_unit_count(
                _unit, [&](Role *r) { return RoleCow::cast(r) && RoleCow::cast(r)->repair; })
            >= max_count)) return false;

    Cell *best_cell = NULL;
    int min_steps = INT_MAX;
    int max_radius = 1;
    Cell *cell = factory->radius_cell(max_radius);
    while (cell) {
        int dist = cur_cell->man_dist(cell);
        if (dist <= 8
            && cell->rubble > 0
            && cell->rubble <= 20
            && !cell->ice
            && !cell->ore
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))) {
            int steps = dist + cell->rubble / _unit->cfg->DIG_RUBBLE_REMOVED;
            if (steps < min_steps) {
                min_steps = steps;
                best_cell = cell;
            }
        }
        cell = factory->radius_cell(max_radius, cell);
    }

    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RoleCow(_unit, factory, best_cell, /*repair*/true);
        return true;
    }

    return false;
}

void RoleCow::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    string r = this->repair ? "r" : "";
    os << "Cow[" << *this->factory << fgoal << " -> "
       << *this->rubble_cell << r << cgoal << "]";
}

Factory *RoleCow::get_factory() {
    return this->factory;
}

double RoleCow::power_usage() {
    int dist = this->rubble_cell->man_dist_factory(this->factory);
    int dig_power = MAX(0, (2 * this->unit->cfg->BATTERY_CAPACITY / 3
                            - 2 * dist * this->unit->cfg->MOVE_COST));
    int dig_count = dig_power / this->unit->cfg->DIG_COST;
    return ((dig_count * this->unit->cfg->DIG_COST
             + 2 * dist * this->unit->cfg->MOVE_COST)
            / (dig_count + 2 * dist + 1));
}

void RoleCow::set() {
    Role::set();
    this->rubble_cell->set_unit_assignment(this->unit);
}

void RoleCow::unset() {
    if (this->is_set()) {
        this->rubble_cell->unset_unit_assignment(this->unit);
        Role::unset();
    }
}

void RoleCow::teardown() {
}

bool RoleCow::is_valid() {
    bool is_valid = (this->factory->alive()
                     && this->rubble_cell->rubble > 0);

    // Heavies should revert to ice mining if water runs low
    if (is_valid
        && this->unit->heavy
        && this->factory->total_water() < 40) {
        int heavy_ice_miner_count = 0;  // cannot use factory.heavy_ice_miner_count in is_valid
        for (Unit *u : this->factory->heavies) {
            if (RoleMiner::cast(u->role)
                && RoleMiner::cast(u->role)->resource_cell->ice) heavy_ice_miner_count++;
        }
        if (heavy_ice_miner_count == 0) {
            is_valid = false;
        }
    }

    return is_valid;
}

Cell *RoleCow::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->factory->cell) return this->rubble_cell;

    // Goal is rubble cell
    if (this->goal_type == 'c') return this->rubble_cell;

    // Goal is factory
    return this->factory->cell;
}

void RoleCow::update_goal() {
    if (this->goal_type == 'c') {  // Done with rubble cell goal?
        // Handled by RoleRecharge
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = -1;
        if (this->unit->heavy && this->rubble_cell->man_dist_factory(this->factory) == 1) {
            power_threshold = (this->unit->cfg->ACTION_QUEUE_POWER_COST
                               + 3 * this->unit->cfg->MOVE_COST
                               + 2 * this->unit->cfg->DIG_COST
                               + this->rubble_cell->rubble);
        } else {
            power_threshold = 10 * this->unit->cfg->DIG_COST;
        }
        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold) {
            this->goal_type = 'c';
            this->goal = this->rubble_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleCow::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleCow::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleCow::do_move B");
    return false;
}

bool RoleCow::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleCow::do_dig A");
    if (this->goal_type == 'c' && this->unit->cell() == this->rubble_cell) {
        if (this->unit->power >= this->unit->dig_cost()) {
            this->unit->do_dig();
            return true;
        }
    }
    return false;
}

bool RoleCow::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleCow::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleCow::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleCow::do_pickup A");
    return this->_do_power_pickup();
}
