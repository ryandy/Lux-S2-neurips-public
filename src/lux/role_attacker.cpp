#include "lux/role_attacker.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleAttacker::RoleAttacker(Unit *_unit, Factory *_factory, Unit *_target_unit,
                           bool _low_power_attack, bool _defend)
    : Role(_unit, 'u'), factory(_factory), target_unit(_target_unit),
      low_power_attack(_low_power_attack), defend(_defend)
{
    this->goal = _target_unit;
}

bool RoleAttacker::from_transition_low_power_attack(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleAttacker::from_transition_low_power_attack A");
    if (!board.sim0() || board.final_night()) return false;

    Factory *factory = _unit->assigned_factory;

    // Some roles/situations should not transition
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair
                && _unit->heavy)
            || RolePincer::cast(_unit->role)
            || RoleProtector::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    // Maybe don't transition lone heavy ice miner
    if (factory->water < 200
        && factory->heavy_ice_miner_count == 1
        && _unit->heavy
        && RoleMiner::cast(_unit->role)
        && RoleMiner::cast(_unit->role)->resource_cell->ice) {
        return false;
    }

    // Don't abandon factory that needs water soon
    if (factory->water < 75
        && factory->heavy_ice_miner_count == 0
        && _unit->heavy) {
        return false;
    }

    // Limit potential targets for some ice conflict heavies
    Factory *ice_conflict_target_factory = NULL;
    if (_unit->heavy
        && ModeIceConflict::cast(factory->mode)
        && (factory->heavy_antagonizer_count == 0
            || RoleAntagonizer::cast(_unit->role))) {
        ice_conflict_target_factory = ModeIceConflict::cast(factory->mode)->opp_factory;
    }

    Cell *cur_cell = _unit->cell();
    for (Unit *opp_unit : board.opp->units()) {
        Cell *opp_cell = opp_unit->cell();
        if (!opp_unit->low_power
            || opp_cell->factory
            || opp_unit->heavy != _unit->heavy) continue;

        int cur_cell_opp_dist = cur_cell->man_dist(opp_cell);
        if (RoleBlockade::cast(_unit->role)
            && opp_unit->assigned_unit
            && cur_cell_opp_dist < opp_unit->assigned_unit->cell()->man_dist(opp_cell)) {
            // can displace
        } else if (opp_unit->assigned_unit) {
            continue;
        }

        Factory *opp_factory = opp_unit->last_factory;  // nearest?
        int opp_dist = opp_cell->man_dist_factory(opp_factory);

        // Only transition blockade units if their mission continues
        if (RoleBlockade::cast(_unit->role)) {
            // only accept an attack for existing target unit
            if (opp_unit != RoleBlockade::cast(_unit->role)->target_unit) continue;
            int move_cost = 6;
            for (Cell *neighbor : opp_cell->neighbors) {
                move_cost = MIN(move_cost, _unit->move_basic_cost(neighbor));
            }
            // don't attack until they're truly stuck
            if (opp_unit->power >= move_cost) continue;
        }

        // Don't let ice conflict heavies wander
        if (ice_conflict_target_factory && opp_factory != ice_conflict_target_factory) continue;

        // Ignore units that have no way to refuel
        if (board.sim_step + opp_dist >= FINAL_NIGHT_PHASE) continue;
        if (board.final_night() && opp_unit->power < opp_unit->cfg->RAZE_COST) continue;

        // Ignore heavy miners that may be receiving power from transporters
        // Check for transporters (or dist1)
        bool is_heavy_miner = false;
        if (opp_unit->heavy
            && (opp_cell->ice || opp_cell->ore)
            && !opp_unit->mine_cell_steps.empty()
            && opp_unit->mine_cell_steps.back().second >= board.sim_step - 5) {
            if (opp_cell->away_dist == 1
                || cur_cell_opp_dist >= 10) continue;
            auto it = board.opp_chains.find(opp_unit);
            if (it != board.opp_chains.end()) {
                // There exists a chain to this heavy miner
                // If the chain is un-antagonized, continue
                bool is_antagonized = false;
                for (Cell *chain_cell : *it->second) {
                    if (chain_cell->assigned_unit
                        && (chain_cell->assigned_unit->power
                            >= 20 * chain_cell->assigned_unit->cfg->MOVE_COST)
                        && RoleAntagonizer::cast(chain_cell->assigned_unit->role)) {
                        is_antagonized = true;
                    }
                }
                if (!is_antagonized) continue;
            }
            is_heavy_miner = true;
        }

        // Ignore distant heavies during end phase
        if (opp_unit->heavy
            && cur_cell_opp_dist >= 10
            && board.sim_step >= END_PHASE) continue;

        // Ignore if could not return after kill
        if (2 * cur_cell_opp_dist > 1000 - board.sim_step) continue;

        // Ignore if cannot cut off before recharged
        Cell *cutoff_cell = RoleAttacker::_cutoff_cell(opp_unit);
        int steps_until_safe = opp_unit->steps_until_power(opp_unit->low_power_threshold) + 1;
        int cur_cell_cutoff_dist = cur_cell->man_dist(cutoff_cell);
        if (cur_cell_cutoff_dist >= steps_until_safe) continue;

        // Determine more accurate cutoff cell (important when opp unit has no chance of getting close)
        int opp_power = opp_unit->power;
        int route_idx = 1;
        int pursuit_step = board.sim_step;
        int steps_delayed = 0;
        while (steps_delayed < cur_cell_opp_dist && opp_cell != cutoff_cell) {
            LUX_ASSERT(route_idx < opp_unit->low_power_route.size());
            Cell *next_cell = opp_unit->low_power_route[route_idx];
            int power_to_move = opp_unit->move_basic_cost(next_cell);

            // Decrement power if move is possible this step
            if (opp_power >= power_to_move) {
                opp_cell = next_cell;
                opp_power -= power_to_move;
                route_idx++;
            } else {
                steps_delayed++;
            }

            // Increment power if daytime this step
            opp_power += opp_unit->power_gain(pursuit_step++);
        }
        // Update cutoff_cell and revert opp_cell to proper value
        cutoff_cell = opp_cell;
        opp_cell = opp_unit->cell();

        if (cutoff_cell->factory) return false;

        // TODO: check if unit can/should go to factory first (need time and high factory power)
        int naive_power_threshold = 0;
        if (RoleBlockade::cast(_unit->role) && opp_unit->water >= 5) {
            naive_power_threshold = (
                _unit->cfg->ACTION_QUEUE_POWER_COST
                + board.naive_cost(_unit, cur_cell, opp_cell)
                + board.naive_cost(_unit, opp_cell, cutoff_cell));
        } else {
            naive_power_threshold = (
                3 * _unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * _unit->cfg->MOVE_COST
                + board.naive_cost(_unit, cur_cell, opp_cell)
                + board.naive_cost(_unit, opp_cell, cutoff_cell)
                + board.naive_cost(_unit, cutoff_cell, factory->cell));
        }
        if (_unit->power >= naive_power_threshold) {
            Role::_displace_unit(opp_unit);
            *new_role = new RoleAttacker(_unit, factory, opp_unit, /*low_power*/true, /*defend*/false);
            if (is_heavy_miner) LUX_LOG(*_unit << " attack miner! " << **new_role);
            return true;
        }
    }

    return false;
}

bool RoleAttacker::from_transition_defend_territory(Role **new_role, Unit *_unit, int max_count) {
    //if (_unit->_log_cond()) LUX_LOG("RoleAttacker::from_transition_defend_territory A");
    if (!board.sim0()) return false;

    Factory *factory = _unit->assigned_factory;

    // Some roles/situations should not transition
    if (_unit->role
        && (false
            || (ModeIceConflict::cast(factory->mode)
                && _unit->heavy)
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || RoleAttacker::cast(_unit->role)
            || RoleBlockade::cast(_unit->role)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || (RoleMiner::cast(_unit->role)
                && RoleMiner::cast(_unit->role)->resource_cell->ore
                && _unit->heavy)
            || RolePincer::cast(_unit->role)
            || RoleProtector::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || (RoleRelocate::cast(_unit->role)
                && ModeIceConflict::cast(RoleRelocate::cast(_unit->role)->target_factory->mode))
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    // Maybe don't transition lone heavy ice miner
    if (factory->water < 250
        && factory->heavy_ice_miner_count == 1
        && _unit->heavy
        && RoleMiner::cast(_unit->role)
        && RoleMiner::cast(_unit->role)->resource_cell->ice) {
        return false;
    }

    // Don't abandon factory that needs water soon
    if (factory->water < 250
        && factory->heavy_ice_miner_count == 0
        && _unit->heavy) {
        return false;
    }

    Unit *best_unit = NULL;
    double best_score = INT_MAX;
    Cell *cur_cell = _unit->cell();
    for (Unit *opp_unit : board.opp->units()) {
        Cell *opp_cell = opp_unit->cell();
        int opp_dist = opp_cell->man_dist(cur_cell);
        if (_unit->heavy != opp_unit->heavy
            || opp_dist > 10
            || opp_cell->factory
            || opp_unit->assigned_unit) continue;

        if (board.final_night() && opp_unit->power < opp_unit->cfg->RAZE_COST) continue;

        bool is_qualified = (opp_unit->water >= 5);
        if (!is_qualified) {
            bool lichen_adj = false;
            for (Cell *neighbor : opp_cell->neighbors_plus) {
                if (neighbor->factory == factory
                    || neighbor->lichen_strain == factory->id) {
                    lichen_adj = true;
                    break;
                }
            }
            if (lichen_adj && opp_cell->man_dist_factory(factory) < opp_cell->away_dist) {
                is_qualified = true;
            }
        }

        if (is_qualified) {
            double score = opp_dist + 3 * opp_unit->power / (double)opp_unit->cfg->BATTERY_CAPACITY;
            if (opp_unit->water >= 5) score -= 10;
            if (score < best_score) {
                best_score = score;
                best_unit = opp_unit;
            }
        }
    }

    if (best_unit) {
        *new_role = new RoleAttacker(_unit, factory, best_unit, /*low_power*/false, /*defend*/true);
        return true;
    }

    return false;
}

Cell *RoleAttacker::_cutoff_cell(Unit *opp_unit) {
    LUX_ASSERT(opp_unit->low_power);
    LUX_ASSERT(!opp_unit->low_power_route.empty());

    for (int i = (int)opp_unit->low_power_route.size() - 1; i >= 0; i--) {
        Cell *route_cell = opp_unit->low_power_route[i];
        if (!route_cell->factory) return route_cell;
    }

    LUX_LOG("Bad low power route? " << *opp_unit << ' '
            << opp_unit->low_power << ' '
            << opp_unit->low_power_threshold << ' '
            << opp_unit->low_power_route.size());
    for (int i = (int)opp_unit->low_power_route.size() - 1; i >= 0; i--) {
        Cell *route_cell = opp_unit->low_power_route[i];
        LUX_LOG("  " << *route_cell);
    }
    LUX_ASSERT(false);
    return NULL;
}

void RoleAttacker::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string ugoal = this->goal_type == 'u' ? "*" : "";
    string mod = this->defend ? "def" : "low";
    os << "Attacker[" << *this->factory << fgoal << " -> "
       << *this->target_unit << "(" << mod << ")" << ugoal << "]";
}

Factory *RoleAttacker::get_factory() {
    return this->factory;
}

double RoleAttacker::power_usage() {
    return (0.5 * this->unit->cfg->ACTION_QUEUE_POWER_COST
            + 2 * this->unit->cfg->MOVE_COST);
}

void RoleAttacker::set() {
    Role::set();
    this->target_unit->set_unit_assignment(this->unit);
}

void RoleAttacker::unset() {
    if (this->is_set()) {
        this->target_unit->unset_unit_assignment(this->unit);
        Role::unset();
    }
}

void RoleAttacker::teardown() {
}

bool RoleAttacker::is_valid() {
    // No new info after sim step 0
    if (!board.sim0()) return true;

    // Switch into low power attack mode if possible
    if (!this->low_power_attack
        && this->target_unit->low_power
        && board.sim_step < END_PHASE) {
        this->low_power_attack = true;
        this->defend = false;
        LUX_LOG("attacker modify " << *this->unit << " to low power attack " << *this);
    }

    // Try to keep low-power attackers valid even if home factory is lost
    if (this->low_power_attack
        && !this->factory->alive()
        && this->factory != this->unit->assigned_factory) {
        this->factory = this->unit->assigned_factory;
        LUX_LOG("attacker modify " << *this->unit << " home factory " << *this);
    }

    bool is_valid = (this->factory->alive()
                     && this->target_unit->alive()
                     && !this->target_unit->cell()->factory);

    if (is_valid
        && this->defend) {
        Cell *opp_cell = this->target_unit->cell();
        bool is_qualified = (this->target_unit->water >= 5);
        if (!is_qualified) {
            for (Cell *neighbor : opp_cell->neighbors_plus) {
                if (neighbor->factory == this->factory
                    || neighbor->lichen_strain == this->factory->id) {
                    is_qualified = true;
                    break;
                }
            }
        }
        if (!is_qualified) {
            if (opp_cell->man_dist_factory(this->factory) <= opp_cell->away_dist) {
                is_qualified = true;
            }
        }
        if (!is_qualified) {
            is_valid = false;
        }
    }

    if (is_valid
        && this->defend
        && this->unit->heavy
        && this->factory->total_water() < 40) {
        int heavy_ice_miner_count = 0;  // cannot use factory.heavy_ice_miner_count in is_valid
        for (Unit *u : this->factory->heavies) {
            if (!u->role
                || (RoleMiner::cast(u->role)
                    && RoleMiner::cast(u->role)->resource_cell->ice)) heavy_ice_miner_count++;
        }
        if (heavy_ice_miner_count == 0) {
            is_valid = false;
            LUX_LOG("attacker invalidate (water) " << *this->unit << ' ' << *this);
        }
    }

    // No need to pursue stranded units during final night
    if (is_valid
        && board.final_night()
        && this->low_power_attack) {
        is_valid = false;
    }

    // No need to pursue units unable to destroy lichen during final night
    if (is_valid
        && board.final_night()
        && this->target_unit->power < this->target_unit->cfg->RAZE_COST) {
        is_valid = false;
    }

    return is_valid;
}

// TODO: if low_power_attack and target_unit's AQ has them returning to factory, then try to cut off
Cell *RoleAttacker::goal_cell() {
    Cell *cur_cell = this->unit->cell();
    Cell *opp_cell = this->target_unit->cell();

    // Override goal if on factory center
    if (cur_cell == this->factory->cell) return opp_cell;

    // Goal is target unit
    if (this->goal_type == 'u') {
        // Once we reach cell where opp used to be, try to move toward where they will be
        // TODO predict if they are fleeing or oscillating based on AQ
        if (this->low_power_attack && cur_cell == opp_cell) {
            return this->target_unit->last_factory->cell;
        }
        return opp_cell;
    }

    // Goal is factory
    return this->factory->cell;
}

void RoleAttacker::update_goal() {
    if (this->goal_type == 'u') {  // Done with target unit goal?
        // Handled by RoleRecharge
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = -1;
        if (this->factory->power >= 5000) {
            power_threshold = 0.95 * this->unit->cfg->BATTERY_CAPACITY;
        } else {
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + board.naive_cost(this->unit, this->unit->cell(), this->target_unit->cell())
                + 20 * this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 20 * this->unit->cfg->MOVE_COST
                + board.naive_cost(this->unit, this->target_unit->cell(), this->factory->cell));
        }
        power_threshold = MIN(power_threshold, this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold) {
            this->goal_type = 'u';
            this->goal = this->target_unit;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleAttacker::do_move() {    
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleAttacker::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleAttacker::do_move B");
    return false;
}

bool RoleAttacker::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAttacker::do_dig A");
    return false;
}

bool RoleAttacker::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAttacker::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleAttacker::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAttacker::do_pickup A");
    return this->_do_power_pickup();
}
