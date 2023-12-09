#include "lux/role_chain_transporter.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleChainTransporter::RoleChainTransporter(Unit *_unit, Factory *_factory, Cell *_target_cell,
                                           Unit *_target_unit, int _chain_idx)
    : Role(_unit, 'f'), factory(_factory), target_cell(_target_cell),
      target_unit(_target_unit), chain_idx(_chain_idx)
{
    this->goal = _factory;
}

bool RoleChainTransporter::from_miner(Role **new_role, Unit *_unit, int max_dist) {
    if (_unit->heavy) return false;
    //if (_unit->_log_cond()) LUX_LOG("RoleChainTransporter::from_miner A");

    Factory *factory = _unit->assigned_factory;
    Cell *cur_cell = _unit->cell();

    int best_score = INT_MIN;
    RoleMiner *best_miner = NULL;
    int best_chain_idx = -1;
    RoleMiner *role_miner;
    for (Unit *miner_unit : factory->heavies) {
        if (!(role_miner = RoleMiner::cast(miner_unit->role))
            || role_miner->chain_route.empty()) continue;

        for (size_t i = 0; i < role_miner->chain_units.size(); i++) {
            if (role_miner->chain_units[i] == NULL) {
                int dist = cur_cell->man_dist(role_miner->chain_route[i]);
                if (!max_dist || dist <= max_dist) {
                    int score = -dist;
                    if (score > best_score) {
                        best_score = score;
                        best_miner = role_miner;
                        best_chain_idx = i;
                    }
                }
            }
        }
    }

    if (best_miner) {
        *new_role = new RoleChainTransporter(_unit, factory, best_miner->chain_route[best_chain_idx],
                                             best_miner->unit, best_chain_idx);
        return true;
    }

    return false;
}

bool RoleChainTransporter::from_transition_partial_chain(Role **new_role, Unit *_unit, int max_dist) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);

    // Some roles/situations should not transition (recharge can)
    if (_unit->role
        && (false
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack)
            || RoleBlockade::cast(_unit->role)
            || RoleChainTransporter::cast(_unit->role)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || RolePincer::cast(_unit->role)
            || RolePowerTransporter::cast(_unit->role)
            || RoleRelocate::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    return RoleChainTransporter::from_miner(new_role, _unit, max_dist);
}

void RoleChainTransporter::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    os << "ChainTransporter[" << *this->factory << fgoal << " -> "
       << *this->target_unit << *this->target_cell << cgoal << "]";
}

Factory *RoleChainTransporter::get_factory() {
    return this->factory;
}

void RoleChainTransporter::set() {
    Role::set();
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    role_miner->set_chain_transporter(this->unit, this->chain_idx);
}

void RoleChainTransporter::unset() {
    if (this->is_set()) {
        RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
        if (role_miner) role_miner->unset_chain_transporter(this->chain_idx);
        Role::unset();
    }
}

void RoleChainTransporter::teardown() {
}

bool RoleChainTransporter::is_valid() {
    return (this->get_factory()
            && this->get_factory()->alive()
            && this->target_unit->alive()
            && RoleMiner::cast(this->target_unit->role));
}

Cell *RoleChainTransporter::goal_cell() {
    if (this->goal_type == 'c') return this->target_cell;

    // Goal is factory
    return this->factory->cell;
}

void RoleChainTransporter::update_goal() {
    if (this->goal_type == 'c') {  // Done with target cell/unit goal?
        int power_threshold = 0;
        if (this->unit->cell() != this->target_cell) {
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + 3 * this->unit->cfg->MOVE_COST
                + board.naive_cost(this->unit, this->unit->cell(), this->target_cell));
        }
        if (this->unit->power < power_threshold) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = (
            this->unit->cfg->ACTION_QUEUE_POWER_COST
            + 10 * this->unit->cfg->MOVE_COST
            + board.naive_cost(this->unit, this->unit->cell(), this->target_cell));
        if (this->unit->power >= power_threshold) {
            this->goal_type = 'c';
            this->goal = this->target_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleChainTransporter::do_move() {
    // If receiving resources, don't lock in a move (allow for pickup/transfer)
    if (this->unit->ore_delta > 0 || this->unit->ice_delta > 0) {
        //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move A");
        return false;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move B");
    return this->_do_move();
}

bool RoleChainTransporter::do_move_last_chain() {
    if (this->unit->cell()->man_dist(this->target_cell) > 1) return false;
    if (this->target_unit->cell()->man_dist(this->target_cell) != 1) return false;
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);
    if (this->chain_idx != role_miner->chain_units.size() - 1) return false;

    // If miner is threatened and on the move, this is no longer a high priority action
    if (board.sim0() && this->target_unit->move_risk(this->target_unit->cell())) return false;

    return this->_do_move();
}

// Specifically if at target_cell but cannot stay there - this move is high-priority to prevent bad tx.
bool RoleChainTransporter::do_move_if_threatened(bool last_chain_only) {
    if (last_chain_only) {
        if (this->target_unit->cell()->man_dist(this->target_cell) != 1) return false;
        RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
        LUX_ASSERT(role_miner);
        if (this->chain_idx != role_miner->chain_units.size() - 1) return false;
    }

    // If miner is threatened and on the move, this is no longer a high priority action
    if (board.sim0() && this->target_unit->move_risk(this->target_unit->cell())) return false;

    Cell *cur_cell = this->unit->cell();
    if (cur_cell == this->target_cell
        && (!this->unit->move_is_safe_from_friendly_fire(cur_cell)
            || this->unit->move_risk(cur_cell) > 0)) {
        //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move_threatened A");
        return this->_do_move(/*goal_cell*/cur_cell, /*allow_no_move*/true);
    }
    //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move_threatened B");
    return false;
}

bool RoleChainTransporter::do_no_move_if_receiving() {
    if (this->unit->ore_delta > 0 || this->unit->ice_delta > 0) {
        //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move_receiving A");
        return this->_do_no_move();
    }
    //if (this->unit->_log_cond()) LUX_LOG("RCT::do_move_receiving B");
    return false;
}

bool RoleChainTransporter::do_dig() {
    Cell *cur_cell = this->unit->cell();
    if (cur_cell == this->target_cell
        && this->unit->power >= 2 * this->unit->cfg->DIG_COST
        && cur_cell->rubble > 0
        && cur_cell->away_dist >= 10
        && board.sim_step >= 50
        && this->unit->power >= this->unit->dig_cost()) {
        this->unit->do_dig();
        return true;
    }
    return false;
}

Cell *RoleChainTransporter::_get_factory_bound_cell(Unit **factory_bound_unit_out) {
    if (this->chain_idx == 0) return this->target_cell;
    else if (this->chain_idx == 1) return this->target_cell->neighbor_toward(this->factory->cell);

    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);

    Unit *factory_bound_unit = role_miner->chain_units[this->chain_idx - 1];
    Cell *factory_bound_cell = role_miner->chain_route[this->chain_idx - 1];
    if (!factory_bound_unit) return NULL;

    Cell *cur_cell = factory_bound_unit->cell();
    Cell *next_cell = factory_bound_unit->cell_next();
    if (next_cell == factory_bound_cell  // moving to cell
        || (cur_cell == factory_bound_cell && next_cell == NULL)) {  // at cell, no move yet
        *factory_bound_unit_out = factory_bound_unit;
        return factory_bound_cell;
    }

    return NULL;
}

Cell *RoleChainTransporter::_get_miner_bound_cell(Unit **miner_bound_unit_out) {
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);

    Unit *miner_bound_unit;
    Cell *miner_bound_cell;
    if (this->chain_idx == (int)role_miner->chain_route.size() - 1) {
        miner_bound_unit = role_miner->unit;
        miner_bound_cell = role_miner->resource_cell;
    } else {
        miner_bound_unit = role_miner->chain_units[this->chain_idx + 1];
        miner_bound_cell = role_miner->chain_route[this->chain_idx + 1];
    }
    if (!miner_bound_unit) return NULL;

    Cell *cur_cell = miner_bound_unit->cell();
    Cell *next_cell = miner_bound_unit->cell_next();
    if (next_cell == miner_bound_cell  // moving to cell
        || (cur_cell == miner_bound_cell && next_cell == NULL)) {  // at cell, no move yet
        *miner_bound_unit_out = miner_bound_unit;
        return miner_bound_cell;
    }

    return NULL;
}

bool RoleChainTransporter::do_transfer() {
    if (this->goal_type != 'c'
        || this->unit->cell() != this->target_cell) return false;

    // Transfer resources toward factory if has resources
    Unit *factory_bound_unit = NULL;
    Cell *factory_bound_cell = this->_get_factory_bound_cell(&factory_bound_unit);
    if (factory_bound_cell
        && this->_do_transfer_resource_to_factory(factory_bound_cell, factory_bound_unit)) {
        return true;
    }

    // TODO: still do transfer if target unit (or (any?) miner bound unit) is low power?
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);
    if (role_miner->_ore_chain_is_paused()) return false;

    // Transfer power toward miner if has power
    Unit *miner_bound_unit = NULL;
    Cell *miner_bound_cell = this->_get_miner_bound_cell(&miner_bound_unit);
    if (miner_bound_cell) {
        LUX_ASSERT(miner_bound_unit);
        int amount = (miner_bound_unit->cfg->BATTERY_CAPACITY
                      - miner_bound_unit->power
                      - miner_bound_unit->power_delta
                      - miner_bound_unit->power_gain());
        int dist = this->target_cell->man_dist_factory(this->get_factory());
        int power_gain = this->unit->power_gain(board.sim_step, board.sim_step + dist);
        int power_to_keep = (this->unit->cfg->ACTION_QUEUE_POWER_COST
                             + 3 * this->unit->cfg->MOVE_COST
                             + 6 * dist * this->unit->cfg->MOVE_COST
                             - power_gain);
        amount = MIN(amount, this->unit->power - power_to_keep);
        if (amount > 5
            && (this->unit->power
                >= this->unit->transfer_cost(miner_bound_cell, Resource_POWER, amount))) {
            this->unit->do_transfer(miner_bound_cell, Resource_POWER, amount, miner_bound_unit);
            return true;
        }
    }

    return false;
}

bool RoleChainTransporter::do_pickup() {
    if (this->chain_idx == 0) LUX_ASSERT(this->target_cell->factory);
    if (this->target_cell->factory) LUX_ASSERT(this->chain_idx == 0);

    if (this->goal_type == 'f') {
        return this->_do_power_pickup();
    } else if (this->chain_idx == 0) {
        // TODO: still do pickup if target unit is low power?
        RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
        LUX_ASSERT(role_miner);
        if (role_miner->_ore_chain_is_paused()) return false;

        // Skip pickup if miner has sufficient power
        int digs_remaining = this->target_unit->power / this->target_unit->cfg->DIG_COST;
        int power_gain = this->target_unit->power_gain(board.sim_step, board.sim_step+digs_remaining);
        digs_remaining = (this->target_unit->power + power_gain) / this->target_unit->cfg->DIG_COST;
        if (digs_remaining >= 8 + (int)role_miner->chain_route.size() - 1) return false;

        return this->_do_power_pickup(/*max_amount*/INT_MAX, /*goal_is_factory_override*/true);
    }

    return false;
}
