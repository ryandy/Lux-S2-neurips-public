#include "lux/role_pillager.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_chain_transporter.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_defender.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pillager.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RolePillager::RolePillager(Unit *_unit, Factory *_factory, Cell *_lichen_cell)
    : Role(_unit), factory(_factory), lichen_cell(_lichen_cell)
{
    int fdist = _unit->cell()->man_dist_factory(_factory);
    int cdist = _unit->cell()->man_dist(_lichen_cell);
    if (fdist < cdist) {
        this->goal_type = 'f';
        this->goal = _factory;
    } else {
        this->goal_type = 'c';
        this->goal = _lichen_cell;
    }
}

bool RolePillager::from_lichen(Role **new_role, Unit *_unit, int max_dist, int max_count, bool _bn) {
    //if (_unit->_log_cond()) LUX_LOG("RolePillager::from_lichen A");
    Factory *factory = _unit->assigned_factory;
    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RolePillager::cast(r); }) >= max_count) return false;

    // Modify max_dist for transitions
    if (max_dist < 100 && RolePillager::cast(_unit->role)) {
        max_dist = RolePillager::cast(_unit->role)->lichen_cell->man_dist_factory(factory) + 5;
    }

    int steps_remaining = 1000 - board.sim_step;
    Cell *cur_cell = _unit->cell();
    bool cur_near_factory = cur_cell->man_dist_factory(factory) <= max_dist;

    Cell *best_cell = NULL;
    double best_score = INT_MIN;
    for (Factory *opp_factory : board.opp->factories()) {
        for (Cell *cell : opp_factory->lichen_connected_cells) {
            int cur_cell_dist = cell->man_dist(cur_cell);
            if (cell->man_dist_factory(factory) > max_dist
                || (!cur_near_factory && cur_cell_dist > max_dist)) continue;

            // Check if significant bottleneck is required
            if (_bn
                && (cell->lichen_bottleneck_step != board.step
                    || cell->lichen_bottleneck_cell_count < 25)) continue;

            Unit *assigned_unit = cell->assigned_unit;
            if (cur_cell_dist <= steps_remaining - 1
                && (!assigned_unit
                    || (_unit->heavy && !assigned_unit->heavy)
                    || (board.final_night()
                        && _unit->heavy == assigned_unit->heavy
                        && _unit->power >= _unit->cfg->DIG_COST
                        && (assigned_unit->power < assigned_unit->cfg->DIG_COST
                            || cur_cell_dist < assigned_unit->cell()->man_dist(cell))))) {
                double score = RolePillager::_cell_score(_unit, cell);
                if (score > best_score) {
                    best_score = score;
                    best_cell = cell;
                }
            }
        }
    }

    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RolePillager(_unit, factory, best_cell);
        return true;
    }

    // During endgame also check for dist-1 boundary cells in some cases
    if (_bn || board.sim_step < END_PHASE) return false;
    if (_unit->heavy && max_dist < 100) return false;
    if (RolePillager::cast(_unit->role)
        && RolePillager::cast(_unit->role)->lichen_cell->away_dist == 1) return false;

    best_cell = NULL;
    best_score = INT_MIN;
    for (Factory *opp_factory : board.opp->factories()) {
        Cell *cell = opp_factory->radius_cell(1);
        while (cell) {
            if (cell->lichen <= 0 && !cell->ice && !cell->ore) {
                int cur_cell_dist = cell->man_dist(cur_cell);
                Unit *assigned_unit = cell->assigned_unit;
                if (cur_cell_dist <= steps_remaining - 1
                    && cell->man_dist_factory(factory) <= max_dist
                    && (cur_near_factory || cur_cell_dist <= max_dist)
                    && (!assigned_unit
                        || (_unit->heavy && !assigned_unit->heavy)
                        || (board.final_night()
                            && _unit->heavy == assigned_unit->heavy
                            && _unit->power >= _unit->cfg->DIG_COST
                            && (assigned_unit->power < assigned_unit->cfg->DIG_COST
                                || cur_cell_dist < assigned_unit->cell()->man_dist(cell))))) {
                    double score = -cur_cell_dist;
                    if (score > best_score) {
                        best_score = score;
                        best_cell = cell;
                    }
                }
            }
            cell = opp_factory->radius_cell(1, cell);
        }
    }
    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RolePillager(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RolePillager::from_lichen_bottleneck(Role **new_role, Unit *_unit, int max_dist, int max_count) {
    return RolePillager::from_lichen(new_role, _unit, max_dist, max_count, true);
}

bool RolePillager::from_transition_active_pillager(Role **new_role, Unit *_unit, int max_dist) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RolePillager::from_transition_active_pillager A");

    RolePillager *role;
    if (!(role = RolePillager::cast(_unit->role))
        || role->lichen_cell->lichen > 0) {
        return false;
    }

    // Usually hang out adj to opp factory unless there is other low hanging fruit
    if (role->lichen_cell->away_dist == 1) {
        bool more_work = false;
        for (Cell *neighbor : role->lichen_cell->neighbors) {
            if (neighbor->away_dist == 1
                && neighbor->lichen > 0
                && !neighbor->assigned_unit) {
                more_work = true;
            }
        }
        if (!more_work) return false;
    }

    // Displaced units may be set to nearby factory center
    int max_radius = (role->lichen_cell->factory_center ? 4 : 2);
    int steps_remaining = 1000 - board.sim_step;
    Cell *cur_cell = _unit->cell();
    for (int radius = 1; radius <= max_radius; radius++) {
        Cell *best_cell = NULL;
        double best_score = INT_MIN;
        Cell *rc = role->lichen_cell->radius_cell(radius, radius);
        while (rc) {
            int cur_cell_dist = cur_cell->man_dist(rc);
            if (rc->lichen > 0
                && board.opp->is_strain(rc->lichen_strain)
                && (!rc->assigned_unit
                    || (_unit->heavy && !rc->assigned_unit->heavy))
                && cur_cell_dist < steps_remaining - 1) {
                double score = RolePillager::_cell_score(_unit, rc);
                if (score > best_score) {
                    best_score = score;
                    best_cell = rc;
                }
            }
            rc = role->lichen_cell->radius_cell(radius, radius, rc);
        }

        if (best_cell) {
            //if (board.sim0()) LUX_LOG(*_unit << " RP transition " << *role->lichen_cell << ' '
            //                          << *best_cell);
            Role::_displace_unit(best_cell);
            *new_role = new RolePillager(_unit, role->factory, best_cell);
            return true;
        }
    }

    if (board.sim_step < END_PHASE) {
        return RolePillager::from_lichen(new_role, _unit, max_dist);
    } else {
        max_dist = (1000 - board.sim_step - 20);
        return (RolePillager::from_transition_end_of_game(new_role, _unit, /*allow*/true)
                || RoleAntagonizer::from_mine(new_role, _unit, Resource_ICE, max_dist)
                || RoleDefender::from_unit(new_role, _unit, max_dist));
    }
}

bool RolePillager::from_transition_end_of_game(Role **new_role, Unit *_unit, bool allow_pillager) {
    //if (_unit->_log_cond()) LUX_LOG("RolePillager::from_transition_end_of_game A");
    if (board.sim_step < END_PHASE) return false;

    Factory *factory = _unit->assigned_factory;

    // Some roles/situations should not transition
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->target_cell->ice
                && RoleAntagonizer::cast(_unit->role)->target_cell->man_dist_factory(
                    _unit->assigned_factory) < 10)
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || RoleAttacker::cast(_unit->role)
            || RoleBlockade::cast(_unit->role)
            || RoleChainTransporter::cast(_unit->role)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || (RoleMiner::cast(_unit->role)
                && (RoleMiner::cast(_unit->role)->resource_cell->ice
                    || _unit->ore))
            || (RolePillager::cast(_unit->role)
                && !allow_pillager)
            || RolePincer::cast(_unit->role)
            || (RolePowerTransporter::cast(_unit->role)
                && (!_unit->heavy
                    || factory->power < 2500))
            || RoleProtector::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || (RoleRelocate::cast(_unit->role)
                && ModeIceConflict::cast(RoleRelocate::cast(_unit->role)->target_factory->mode))
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    // Don't transition until factory has heavy ice miner
    if (factory->heavy_ice_miner_count == 0 && factory->total_water() < 500) return false;

    Cell *best_cell = NULL;
    int best_score = INT_MIN;
    Cell *cur_cell = _unit->cell();
    int unit_factory_dist = _unit->cell()->man_dist_factory(factory);
    for (Factory *opp_factory : board.opp->factories()) {
        for (Cell *cell : opp_factory->lichen_connected_cells) {
            if (cell->assigned_unit) continue;
            int factory_lichen_dist = cell->man_dist_factory(factory);
            int unit_lichen_dist = cur_cell->man_dist(cell);
            int cushion = (board.final_night() ? 2 : 20);
            int end_step = board.sim_step + cushion;
            int score = cell->lichen - cell->lichen_dist;
            if (unit_lichen_dist < unit_factory_dist) {
                end_step += unit_lichen_dist;
                score -= unit_lichen_dist / 5;
            } else {
                end_step += unit_factory_dist + factory_lichen_dist;
                score -= factory_lichen_dist / 5;
            }
            if (end_step < 1000
                && score > best_score) {
                best_score = score;
                best_cell = cell;
            }
        }
    }
    if (best_cell) {
        *new_role = new RolePillager(_unit, factory, best_cell);
        return true;
    }

    return RolePillager::from_lichen(new_role, _unit, /*dist*/100);
}

double RolePillager::_cell_score(Unit *unit, Cell *cell) {
    if (cell->lichen <= 0) return 0;

    int steps_remaining = 1000 - board.sim_step;
    if (board.sim_step > 900
        && cell->lichen_dist == INT_MAX  // disconnected
        && steps_remaining >= cell->lichen) return 0.0001 * cell->lichen;

    Cell *cur_cell = unit->cell();
    Factory *factory = unit->assigned_factory;

    int dist_unit_to_cell = cur_cell->man_dist(cell);    
    int dist_unit_to_factory = cur_cell->man_dist_factory(factory);
    int dist_cell_to_factory = cell->man_dist_factory(factory);

    int cell_lichen = MIN(100, cell->lichen + dist_unit_to_cell);
    int digs_necessary = ((cell_lichen + unit->cfg->DIG_LICHEN_REMOVED - 1 )  // round up
                          / unit->cfg->DIG_LICHEN_REMOVED);
    cell_lichen = MIN(100, cell_lichen + digs_necessary);
    digs_necessary = ((cell_lichen + unit->cfg->DIG_LICHEN_REMOVED - 1 )  // round up
                      / unit->cfg->DIG_LICHEN_REMOVED);

    if (dist_unit_to_cell < dist_unit_to_factory) {
        // Confirm unit has enough power to dig through target cell if not going to factory first
        int power_threshold = (
            2 * unit->cfg->ACTION_QUEUE_POWER_COST
            + 3 * unit->cfg->MOVE_COST
            + 1.5 * unit->cfg->MOVE_COST * dist_unit_to_cell
            + unit->cfg->DIG_COST * digs_necessary
            + 1.5 * unit->cfg->MOVE_COST * dist_cell_to_factory);
        if (unit->power < power_threshold) {
            return 0;
        }
    }

    double cost = (unit->cfg->MOVE_COST * dist_unit_to_cell
                   + unit->cfg->DIG_COST * digs_necessary
                   + 0.25 * unit->cfg->MOVE_COST * dist_cell_to_factory);

    PDD traffic_scores = cell->get_traffic_score(NULL, /*neighbors*/true);
    double traffic_score = 5 * MIN(0.2,
                                   (traffic_scores.first + (unit->heavy ? 0 : traffic_scores.second)));

    double cell_value = 1;
    if (cell->lichen_bottleneck_step == board.step) {
        if (cell->lichen_bottleneck_cell_count >= 2) cell_value += 1;
        if (cell->lichen_bottleneck_cell_count >= 5) cell_value += 1;
        if (cell->lichen_bottleneck_cell_count >= 10) cell_value += 1;
        if (cell->lichen_bottleneck_cell_count >= 25) cell_value += 1;
    }
    if (cell->lichen_opp_boundary_step == board.step) cell_value += 2;
    if (board.sim_step < END_PHASE) {
        if (cell->lichen_frontier_step == board.step) cell_value += 1;
    } else {
        if (cell->lichen_dist <= 3) cell_value += 2;
    }
    if (cell->x % 2 == 0 && cell->y % 2 == 0) cell_value += 0.5;

    cell_value *= (1.1 - traffic_score);
    return cell_value / cost;
}

void RolePillager::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    os << "Pillager[" << *this->factory << fgoal << " -> "
       << *this->lichen_cell << cgoal << "]";
}

Factory *RolePillager::get_factory() {
    return this->factory;
}

double RolePillager::power_usage() {
    if (this->lichen_cell->lichen > 0) {
        return (0.6 * this->unit->cfg->MOVE_COST
                + 0.35 * this->unit->cfg->DIG_COST
                + 0.2 * this->unit->cfg->ACTION_QUEUE_POWER_COST);
    } else {
        return (0.2 * this->unit->cfg->MOVE_COST
                + 0.2 * this->unit->cfg->ACTION_QUEUE_POWER_COST);
    }
}

void RolePillager::set() {
    Role::set();
    this->lichen_cell->set_unit_assignment(this->unit);
}

void RolePillager::unset() {
    if (this->is_set()) {
        this->lichen_cell->unset_unit_assignment(this->unit);
        Role::unset();
    }
}

void RolePillager::teardown() {
}

bool RolePillager::is_valid() {
    bool is_valid = this->factory->alive();

    if (is_valid
        && board.player->is_strain(this->lichen_cell->lichen_strain)) {
        is_valid = false;
    }

    if (is_valid
        && this->unit->cell()->man_dist(this->lichen_cell) >= 1000 - board.sim_step - 1) {
        is_valid = false;
    }

    return is_valid;
}

Cell *RolePillager::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->factory->cell) return this->lichen_cell;

    // Goal is rubble cell
    if (this->goal_type == 'c') return this->lichen_cell;

    // Goal is factory
    return this->factory->cell;
}

void RolePillager::update_goal() {
    if (this->goal_type == 'c') {  // Done with lichen cell goal?
        // Handled by RoleRecharge
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        Cell *cur_cell = this->unit->cell();
        int power_threshold = 0;
        if (board.sim_step >= END_PHASE && this->factory->power < 500) {
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + this->unit->cfg->RAZE_COST
                + board.naive_cost(this->unit, cur_cell, this->lichen_cell)
                - this->unit->power_gain(board.sim_step, 1000));
        } else if (board.sim_step >= END_PHASE) {
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 6 * this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, cur_cell, this->lichen_cell));
        } else {
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 6 * this->unit->cfg->DIG_COST
                + board.naive_cost(this->unit, cur_cell, this->lichen_cell)
                + board.naive_cost(this->unit, this->lichen_cell, this->factory->cell));
        }
        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold) {
            this->goal_type = 'c';
            this->goal = this->lichen_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RolePillager::do_move() {
    if ((this->lichen_cell->lichen > 0
         || (board.sim_step >= END_PHASE
             && this->unit->power >= 6 * this->unit->cfg->MOVE_COST))
        && this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RolePillager::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RolePillager::do_move B");
    return false;
}

bool RolePillager::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePillager::do_dig A");

    bool end_phase = (board.sim_step >= END_PHASE);
    Cell *cur_cell = this->unit->cell();

    if ((this->goal_type == 'c' && cur_cell == this->lichen_cell)
        || (this->unit->heavy && this->unit->cell()->man_dist_factory(this->factory) >= 30)
        || end_phase) {
        if (cur_cell->lichen
            && board.opp->is_strain(cur_cell->lichen_strain)
            && this->unit->power >= this->unit->cfg->DIG_COST) {

            // Try to destroy bottlenecks if possible
            if (end_phase
                && board.sim_step < 970
                && cur_cell->lichen_bottleneck_step == board.step) {
                if (!this->unit->heavy
                    && cur_cell->lichen > this->unit->cfg->DIG_LICHEN_REMOVED
                    && this->unit->power <= 2 * this->unit->cfg->RAZE_COST
                    && this->unit->power >= this->unit->self_destruct_cost()) {
                    this->unit->do_self_destruct();
                    return true;
                } else if (this->unit->power >= this->unit->dig_cost()) {
                    this->unit->do_dig();
                    return true;
                }
            }

            // If endgame before 998, not at goal, and threatened, just return
            // Keep going to goal, then blow up or get killed there
            if (end_phase
                && board.sim_step <= 997
                && cur_cell != this->lichen_cell
                && this->unit->threat_units(this->unit->cell(), 1, 1)) {
                return false;
            }

            // If low power and a self-destruct could do more than digging for the rest of the game
            if (end_phase
                && !this->unit->heavy) {
                int steps_remaining = 1000 - board.sim_step;
                int digs_remaining = this->unit->power / this->unit->cfg->DIG_COST;
                int max_lichen_by_digging = ((this->unit->cfg->DIG_LICHEN_REMOVED - 1)
                                             * MIN(steps_remaining, digs_remaining));
                if (cur_cell->lichen > max_lichen_by_digging
                    && this->unit->power <= 2 * this->unit->cfg->RAZE_COST
                    && this->unit->power >= this->unit->self_destruct_cost()) {
                    this->unit->do_self_destruct();
                    return true;
                }
            }

            // Dig if at goal cell or at any lichen cell during endgame
            if (this->unit->power >= this->unit->dig_cost()) {
                this->unit->do_dig();
                return true;
            }
        }
    }
    return false;
}

bool RolePillager::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePillager::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RolePillager::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePillager::do_pickup A");
    return this->_do_power_pickup();
}
