#include "lux/role_recharge.hpp"

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
#include "lux/role_defender.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pillager.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


bool RoleRecharge::from_unit(Role **new_role, Unit *_unit) {
    *new_role = new RoleRecharge(_unit, _unit->assigned_factory);
    return true;
}

bool RoleRecharge::from_transition_low_power(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleRecharge::from_transition_low_power A");
    if (!_unit->low_power) return false;

    // Was Recharge and made it to the factory
    Factory *factory = _unit->assigned_factory;
    if (!_unit->role && _unit->cell()->factory == factory) return false;

    // Various roles/situations do not utilize Recharge
    RoleMiner *role_miner;
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack
                && (RoleAttacker::cast(_unit->role)->target_unit->water >= 5
                    || (_unit->heavy
                        && RoleAttacker::cast(_unit->role)->target_unit->low_power)))
            || RoleBlockade::cast(_unit->role)
            || RoleChainTransporter::cast(_unit->role)
            || ((role_miner = RoleMiner::cast(_unit->role))
                && (!role_miner->chain_route.empty()
                    || role_miner->resource_cell->man_dist_factory(factory) <= 5))
            || (RolePillager::cast(_unit->role)
                && board.sim_step >= END_PHASE)
            || RolePincer::cast(_unit->role)
            || RolePowerTransporter::cast(_unit->role)
            || RoleProtector::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || RoleRelocate::cast(_unit->role))) {
        return false;
    }

    *new_role = new RoleRecharge(_unit, factory);
    return true;
}

bool RoleRecharge::from_transition_low_water(Role **new_role, Unit *_unit) {
    if (_unit->ice + _unit->water == 0) return false;

    Factory *factory = _unit->assigned_factory;
    Cell *cur_cell = _unit->cell();

    // Various roles/situations do not utilize Recharge
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || RoleChainTransporter::cast(_unit->role)
            || RoleMiner::cast(_unit->role)
            || (RolePillager::cast(_unit->role)
                && board.sim_step >= END_PHASE
                && cur_cell->man_dist_factory(factory) > 10)
            || RolePowerTransporter::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    if (factory->total_water() < 10 + cur_cell->man_dist_factory(factory)) {
        *new_role = new RoleRecharge(_unit, factory);
        return true;
    }

    return false;
}

void RoleRecharge::print(ostream &os) const {
    os << "Recharge[" << *this->factory << "*]";
}

Factory *RoleRecharge::get_factory() {
    return this->factory;
}

double RoleRecharge::power_usage() {
    if (this->unit->cell()->factory == this->factory) return 0;
    return 1.5 * this->unit->cfg->MOVE_COST;
}

void RoleRecharge::set() {
    Role::set();
}

void RoleRecharge::unset() {
    if (this->is_set()) {
        Role::unset();
    }
}

void RoleRecharge::teardown() {
}

bool RoleRecharge::is_valid() {
    return (this->factory->alive()
            && this->unit->power < this->unit->cfg->BATTERY_CAPACITY / 2
            && (this->unit->cell()->factory != this->factory
                || (this->unit->ore + this->unit->ice + this->unit->metal + this->unit->water > 0)));
}

Cell *RoleRecharge::goal_cell() {
    Cell *cur_cell = this->unit->cell();

    // Temporarily override goal cell for ice conflict heavies on the first step
    ModeIceConflict *mode;
    if (board.sim_step == 1
        && this->unit->heavy
        && cur_cell == this->factory->cell
        && (mode = ModeIceConflict::cast(this->factory->mode))) {
        return cur_cell->neighbor_toward(mode->opp_factory->cell);
    }

    // TODO: move heavy ice conflict ant toward opp-side of factory in general?

    // Override goal if on factory center - just move toward center of board
    // Lights will naturally get pushed off by newly created units
    if (this->unit->heavy && cur_cell == this->factory->cell) {
        Cell *mid_cell = board.cell(SIZE / 2, SIZE / 2);
        Cell *rc = mid_cell->radius_cell(SIZE);
        while (rc) {
            if (!rc->factory) return rc;
            rc = mid_cell->radius_cell(SIZE, rc);
        }
    }

    // TODO: do we want to do this?
    // Try to aim for a nearby defender with extra power
    /*int factory_dist = cur_cell->man_dist_factory(this->factory);
    Cell *best_cell = NULL;
    int min_dist = INT_MAX;
    for (Unit *u : this->factory->heavies) {
        if (u == this->unit) continue;
        RoleDefender *r;
        if ((r = RoleDefender::cast(u->role))
            && r->goal_type == 'c'
            && u->cell()->man_dist(r->target_cell) <= 1
            && u->cell()->home_dist > 1
            && u->power >= u->cfg->BATTERY_CAPACITY / 2) {
            int dist = cur_cell->man_dist(u->cell());
            if (dist <= factory_dist && dist < min_dist) {
                min_dist = dist;
                best_cell = u->cell();
            }
        }
    }
    if (best_cell) return best_cell;*/

    // Just aim for factory
    return this->factory->cell;
}

void RoleRecharge::update_goal() {
}

bool RoleRecharge::do_move() {
    // TODO: do we want to do this?
    /*Cell *goal_cell = this->goal_cell();
    if (!goal_cell->factory && this->unit->cell()->man_dist(goal_cell) <= 1) {
        return this->_do_no_move();
    }*/

    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleRecharge::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleRecharge::do_move B");
    return false;
}

bool RoleRecharge::do_dig() {
    return false;
}

bool RoleRecharge::_do_ice_conflict_power_transfer() {
    // Must be factory-bound ice-conflict light
    if (this->unit->heavy
        || !ModeIceConflict::cast(this->factory->mode)
        || this->goal_type != 'f') return false;

    // Must be on factory
    Cell *cur_cell = this->unit->cell();
    if (cur_cell->factory != this->factory) return false;

    // Transfer excess to factory, rounding down to nearest 10
    int amount = this->unit->power - 10;
    amount = (amount / 10) * 10;
    if (amount > 0
        && this->unit->power >= this->unit->transfer_cost(cur_cell, Resource_POWER, amount)) {
        this->unit->do_transfer(cur_cell, Resource_POWER, amount);
        return true;
    }

    return false;
}

bool RoleRecharge::do_transfer() {
    return (this->_do_transfer_resource_to_factory()
            || this->_do_ice_conflict_power_transfer());
}

bool RoleRecharge::do_pickup() {
    return false;
}
