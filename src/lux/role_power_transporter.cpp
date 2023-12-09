#include "lux/role_power_transporter.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_protector.hpp"
#include "lux/unit.hpp"
using namespace std;


RolePowerTransporter::RolePowerTransporter(Unit *_unit, Cell *_factory_cell, Unit *_target_unit)
    : Role(_unit, 'c'), factory_cell(_factory_cell), target_unit(_target_unit)
{
    this->goal = _factory_cell;
}

bool RolePowerTransporter::from_miner(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RolePowerTransporter::from_miner A");
    Factory *factory = _unit->assigned_factory;

    for (Unit *miner_unit : factory->units) {
        if (!miner_unit->heavy) continue;

        RoleMiner *role_miner = RoleMiner::cast(miner_unit->role);
        if (!role_miner
            || role_miner->power_transporter
            || role_miner->protector) continue;

        if (role_miner->resource_cell->man_dist_factory(factory) > 1) continue;

        Cell *factory_cell = role_miner->resource_cell->neighbor_toward(factory->cell);
        LUX_ASSERT(factory_cell->factory);

        // Determine if currently assigned unit can be displaced (chain transporters/miners can be)
        if (factory_cell->assigned_unit) {
            // Cannot displace protectors
            if (RoleProtector::cast(factory_cell->assigned_unit->role)) continue;

            RolePowerTransporter *role_pt = RolePowerTransporter::cast(
                factory_cell->assigned_unit->role);
            // Cannot displace heavy power transporters
            if (role_pt && factory_cell->assigned_unit->heavy) continue;
            // Cannot displace any power transporter for ore
            if (role_pt && role_miner->resource_cell->ore) continue;
            // Cannot displace ice power transporters
            if (role_pt
                && RoleMiner::cast(role_pt->target_unit->role)
                && RoleMiner::cast(role_pt->target_unit->role)->resource_cell->ice) continue;
        }

        Role::_displace_unit(factory_cell);
        *new_role = new RolePowerTransporter(_unit, factory_cell, miner_unit);
        return true;
    }

    return false;
}

bool RolePowerTransporter::from_transition_protector(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RolePowerTransporter::from_transition_protector A");

    RoleProtector *role;
    if (!board.sim0()
        || !_unit->heavy
        || !(role = RoleProtector::cast(_unit->role))) {
        return false;
    }

    if (!role->threat_units(role->miner_unit, /*steps*/15)) {
        *new_role = new RolePowerTransporter(_unit, role->factory_cell, role->miner_unit);
        return true;
    }

    return false;
}

void RolePowerTransporter::print(ostream &os) const {
    if (!this->factory_cell->factory) {
        os << "PowerTransporter[N/A]";
        return;
    }

    string fgoal = this->goal_type == 'c' ? "*" : "";
    string tgoal = this->goal_type == 'u' ? "*" : "";
    os << "PowerTransporter[" << *this->factory_cell->factory << *this->factory_cell << fgoal << " -> "
       << *this->target_unit << tgoal << "]";
}

Factory *RolePowerTransporter::get_factory() {
    return this->factory_cell->factory;
}

void RolePowerTransporter::set() {
    Role::set();
    this->factory_cell->set_unit_assignment(this->unit);
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);
    role_miner->set_power_transporter(this->unit);
}

void RolePowerTransporter::unset() {
    if (this->is_set()) {
        this->factory_cell->unset_unit_assignment(this->unit);
        RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
        if (role_miner) role_miner->unset_power_transporter();
        Role::unset();
    }
}

void RolePowerTransporter::teardown() {
}

bool RolePowerTransporter::is_valid() {
    return (this->get_factory()
            && this->get_factory()->alive()
            && this->target_unit->alive()
            && RoleMiner::cast(this->target_unit->role));
}

Cell *RolePowerTransporter::goal_cell() {
    // Override goal if on factory center
    RoleMiner *target_role = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(target_role);
    if (this->unit->cell() == this->get_factory()->cell) return target_role->resource_cell;

    // Goal is factory cell
    if (this->goal_type == 'c') return this->factory_cell;

    // Goal is target unit:
    // Adjacent to resource cell: no move
    if (this->unit->cell()->man_dist(target_role->resource_cell) == 1) {
        return this->unit->cell();
    }

    // Pick a resource_cell-adjacent destination
    Cell *best_cell = target_role->resource_cell;
    int min_dist = INT_MAX;
    for (Cell *neighbor : target_role->resource_cell->neighbors) {
        if (neighbor->assigned_unit && neighbor->assigned_unit != this->unit) continue;
        if (neighbor->opp_factory()) continue;
        int dist = neighbor->man_dist(this->factory_cell);
        if (dist < min_dist) {
            min_dist = dist;
            best_cell = neighbor;
        }
    }
    return best_cell;
}

void RolePowerTransporter::update_goal() {
    int unit_resource = MAX(this->unit->ice, this->unit->ore);

    if (this->goal_type == 'u') {  // Done with target unit goal?
        int power_threshold = (
            this->unit->cfg->ACTION_QUEUE_POWER_COST
            + this->target_unit->cfg->DIG_COST / 2
            + (2 * this->unit->cfg->MOVE_COST
               * this->unit->cell()->man_dist_factory(this->get_factory())));
        if (this->unit->heavy) power_threshold += this->target_unit->cfg->DIG_COST;
        int resource_threshold = 4 * this->unit->cfg->CARGO_SPACE / 5;
        if (this->unit->power < power_threshold || unit_resource >= resource_threshold) {
            this->goal_type = 'c';
            this->goal = this->factory_cell;
        }
    } else if (this->goal_type == 'c') {  // Done with factory cell goal?
        int power_threshold = (
            this->unit->cfg->ACTION_QUEUE_POWER_COST
            + 2 * this->target_unit->cfg->DIG_COST
            - this->target_unit->power_gain(board.sim_step + 1)
            - this->target_unit->power_gain(board.sim_step + 2));
        if (this->unit->power >= power_threshold && unit_resource == 0) {
            this->goal_type = 'u';
            this->goal = this->target_unit;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RolePowerTransporter::do_move() {
    return this->_do_move();
}

bool RolePowerTransporter::do_dig() {
    return false;
}

bool RolePowerTransporter::_do_excess_power_transfer() {
    // Transfer excess power to factory if factory is low on power
    Cell *cur_cell = this->unit->cell();
    if (this->unit->heavy
        && cur_cell == this->factory_cell
        && this->unit->power >= 1500
        && this->get_factory()->power < 500
        && !ModeIceConflict::cast(this->get_factory()->mode)) {
        int amount = this->unit->power - 700;
        amount = (amount / 10) * 10;  // round down to nearest 10
        if (amount > 0
            && this->unit->power >= this->unit->transfer_cost(cur_cell, Resource_POWER, amount)) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *this->unit->role
                                      << " transporter excess power tx " << amount);
            this->unit->do_transfer(cur_cell, Resource_POWER, amount);
            return true;
        }
    }

    return false;
}

bool RolePowerTransporter::do_transfer() {
    if (this->_do_excess_power_transfer()) {
        return true;
    }

    // TODO: still do transfer if target unit is low power?
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);
    if (role_miner->_ore_chain_is_paused()) return false;

    // Transfer power to target_unit
    if (this->goal_type == 'u') {
        Cell *transfer_cell = this->target_unit->cell_next();
        if (!transfer_cell) {
            LUX_LOG("unit:        " << *this->unit);
            LUX_LOG("unit_role:   " << *this);
            LUX_LOG("unit_cell:   " << *this->unit->cell());
            LUX_LOG("target:      " << *this->target_unit);
            LUX_LOG("target_role: " << *this->target_unit->role);
            LUX_LOG("target_cell: " << *this->target_unit->cell());
        }
        LUX_ASSERT(transfer_cell);
        if (!transfer_cell->factory && this->unit->cell()->man_dist(transfer_cell) == 1) {
            RoleMiner *target_role = RoleMiner::cast(this->target_unit->role);
            LUX_ASSERT(target_role);
            int amount = (this->target_unit->cfg->BATTERY_CAPACITY
                          - this->target_unit->power
                          - this->target_unit->power_delta
                          - this->target_unit->power_gain());
            int dist = target_role->resource_cell->man_dist_factory(this->get_factory());
            int power_gain = this->unit->power_gain(board.sim_step, board.sim_step + dist);
            int power_to_keep = (this->unit->cfg->ACTION_QUEUE_POWER_COST
                                 + 2 * this->unit->cfg->MOVE_COST * dist
                                 - power_gain);
            amount = MIN(amount, this->unit->power - power_to_keep);
            if (amount > 0) {
                if (this->unit->power
                    >= this->unit->transfer_cost(transfer_cell, Resource_POWER, amount)) {
                    this->unit->do_transfer(transfer_cell, Resource_POWER, amount);
                    return true;
                }
            }
        }
    }

    return this->_do_transfer_resource_to_factory();
}

bool RolePowerTransporter::do_pickup() {
    // TODO: still do pickup if target unit is low power?
    RoleMiner *role_miner = RoleMiner::cast(this->target_unit->role);
    LUX_ASSERT(role_miner);
    if (role_miner->_ore_chain_is_paused()) return false;

    // Skip pickup if miner has sufficient power
    int digs_remaining = this->target_unit->power / this->target_unit->cfg->DIG_COST;
    int power_gain = this->target_unit->power_gain(board.sim_step, board.sim_step + digs_remaining);
    digs_remaining = (this->target_unit->power + power_gain) / this->target_unit->cfg->DIG_COST;
    if (digs_remaining >= 8) return false;

    int max_amount = 4 * this->target_unit->cfg->DIG_COST;
    return this->_do_power_pickup(max_amount);
}
