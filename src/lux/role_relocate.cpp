#include "lux/role_relocate.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_recharge.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleRelocate::RoleRelocate(Unit *_unit, Factory *_factory, Factory *_target_factory)
    : Role(_unit, 'f'), factory(_factory), target_factory(_target_factory)
{
    LUX_ASSERT(_factory != _target_factory);
    this->goal = _factory;
}

bool RoleRelocate::from_idle(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleRelocate::from_idle A");

    Factory *factory = _unit->assigned_factory;

    int relocate_lim = _unit->heavy ? 1 : 2;
    if ((_unit->heavy && factory->heavy_relocate_count >= relocate_lim)
        || (!_unit->heavy && factory->light_relocate_count >= relocate_lim)) return false;

    Factory *best_factory = NULL;
    int min_dist = INT_MAX;
    for (Factory *other_factory : board.player->factories()) {
        if (other_factory == factory) continue;
        if ((_unit->heavy && other_factory->heavy_relocate_count > 0)
            || (!_unit->heavy && other_factory->light_relocate_count > 0)) continue;
        int f_power = other_factory->total_power();
        int f_power_income = 0;
        if (f_power < 4000)
            f_power_income = other_factory->power_gain() - other_factory->power_usage();
        if (f_power < 4000 && f_power_income < 20) continue;
        int dist = factory->cell->man_dist(other_factory->cell);
        if (dist < min_dist) {
            min_dist = dist;
            best_factory = other_factory;
        }
    }

    if (best_factory) {
        *new_role = new RoleRelocate(_unit, factory, best_factory);
        if (board.sim0()) LUX_LOG("relocate (idle): " << *_unit << ' ' << **new_role);
        return true;
    }

    return false;
}

bool RoleRelocate::from_ore_surplus(Role **new_role, Unit *_unit) {
    LUX_ASSERT(!_unit->heavy);
    //if (_unit->_log_cond()) LUX_LOG("RoleRelocate::from_ore_surplus A");

    Factory *factory = _unit->assigned_factory;

    if (factory->cell->away_dist < 25
        || factory->heavy_ore_miner_count == 0
        || factory->ore_cells[0]->man_dist_factory(factory) > 5
        || (factory->lights.size() - factory->light_relocate_count) < 6) return false;

    Factory *best_factory = NULL;
    int min_dist = INT_MAX;
    for (Factory *other_factory : board.player->factories()) {
        if (other_factory == factory) continue;

        int light_count = other_factory->lights.size() + other_factory->inbound_light_relocate_count;
        int light_lim = ModeIceConflict::cast(other_factory->mode) ? 5 : 12;
        int dist = factory->cell->man_dist(other_factory->cell);
        if (dist > SIZE
            || light_count >= light_lim
            || other_factory->light_relocate_count > 0
            || other_factory->ore_cells[0]->man_dist_factory(other_factory) <= 5) continue;
        if (dist < min_dist) {
            min_dist = dist;
            best_factory = other_factory;
        }
    }

    if (best_factory) {
        *new_role = new RoleRelocate(_unit, factory, best_factory);
        if (board.sim0()) LUX_LOG("relocate (ore surplus): " << *_unit << ' ' << **new_role);
        return true;
    }

    return false;
}

bool RoleRelocate::from_power_surplus(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleRelocate::from_power_surplus A");

    Factory *factory = _unit->assigned_factory;

    int factory_power = factory->total_power();
    int factory_power_income = factory->power_gain() - factory->power_usage();
    if (factory_power >= 3000 || factory_power_income > 0) return false;

    int relocate_lim = _unit->heavy ? 1 : 2;
    int unit_threshold = _unit->heavy ? 3 : 10;

    if ((_unit->heavy && factory->heavies.size() < unit_threshold)
        || (!_unit->heavy && factory->lights.size() < unit_threshold)) return false;

    if ((_unit->heavy && factory->heavy_relocate_count >= relocate_lim)
        || (!_unit->heavy && factory->light_relocate_count >= relocate_lim)) return false;

    Factory *best_factory = NULL;
    int min_dist = INT_MAX;
    for (Factory *other_factory : board.player->factories()) {
        if (other_factory == factory) continue;

        int f_power = other_factory->total_power();
        int f_power_income = other_factory->power_gain() - other_factory->power_usage();
        if (f_power < 4000 || f_power_income < 20) continue;
        int dist = factory->cell->man_dist(other_factory->cell);
        if (dist < min_dist) {
            min_dist = dist;
            best_factory = other_factory;
        }
    }

    if (best_factory) {
        *new_role = new RoleRelocate(_unit, factory, best_factory);
        if (board.sim0()) LUX_LOG("relocate (power surplus): " << *_unit << ' ' << **new_role);
        return true;
    }

    return false;
}

bool RoleRelocate::from_assist_ice_conflict(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleRelocate::from_assist_ice_conflict A");
    Factory *factory = _unit->assigned_factory;

    if (board.sim_step < 10
        || ModeIceConflict::cast(factory->mode)
        || (_unit->heavy && factory->heavies.size() == 1)) return false;

    Factory *best_factory = NULL;
    int min_dist = INT_MAX;
    for (Factory *other_factory : board.player->factories()) {
        if (other_factory == factory) continue;

        // Relocate up to 1 heavy to any factory
        // Relocate up to 2 heavies to defensive ice conflict
        // Relocate up to 4 lights to any ice conflict

        int count = (_unit->heavy
                     ? other_factory->heavies.size() + other_factory->inbound_heavy_relocate_count
                     : other_factory->lights.size() + other_factory->inbound_light_relocate_count);
        bool is_ice_conflict = ModeIceConflict::cast(other_factory->mode);
        bool is_defensive = (is_ice_conflict
                             && ModeIceConflict::cast(other_factory->mode)->defensive
                             // need a dist-1 ice to do any good as protector
                             && other_factory->ice_cells[0]->man_dist_factory(other_factory) == 1);

        if ((_unit->heavy && count < 1)
            || (_unit->heavy && is_defensive && count < 2)
            || (!_unit->heavy && is_ice_conflict && count < 4)) {
            int dist = factory->cell->man_dist(other_factory->cell);
            if (dist < min_dist) {
                min_dist = dist;
                best_factory = other_factory;
            }
        }
    }

    if (best_factory) {
        *new_role = new RoleRelocate(_unit, factory, best_factory);
        if (board.sim0()) LUX_LOG("relocate (assist): " << *_unit << ' ' << **new_role);
        return true;
    }

    return false;
}

bool RoleRelocate::from_transition_assist_ice_conflict(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleRelocate::from_transition_assist_ice_conflict A");
    Factory *factory = _unit->assigned_factory;

    // Various roles/situations should not transition
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || RolePincer::cast(_unit->role)
            || RoleRecharge::cast(_unit->role)
            || (RoleRelocate::cast(_unit->role)
                && ModeIceConflict::cast(RoleRelocate::cast(_unit->role)->target_factory->mode)))) {
        return false;
    }

    if (factory->heavy_ice_miner_count == 0
        || (factory->heavy_ice_miner_count == 1
            && RoleMiner::cast(_unit->role)
            && RoleMiner::cast(_unit->role)->resource_cell->ice)) {
        return false;
    }

    if (factory->heavies.size() <= 2
        && factory->cell->away_dist <= 10) {
        return false;
    }

    return RoleRelocate::from_assist_ice_conflict(new_role, _unit);
}

void RoleRelocate::print(ostream &os) const {
    string fgoal = this->goal == this->factory ? "*" : "";
    string tgoal = this->goal == this->target_factory ? "*" : "";
    os << "Relocate[" << *this->factory << fgoal << " -> "
       << *this->target_factory << tgoal << "]";
}

Factory *RoleRelocate::get_factory() {
    return this->factory;
}

double RoleRelocate::power_usage() {
    return 1.5 * this->unit->cfg->MOVE_COST;
}

void RoleRelocate::set() {
    Role::set();
    if (this->unit->heavy) {
        this->factory->heavy_relocate_count++;
        this->target_factory->inbound_heavy_relocate_count++;
    } else {
        this->factory->light_relocate_count++;
        this->target_factory->inbound_light_relocate_count++;
    }
}

void RoleRelocate::unset() {
    if (this->is_set()) {
        Role::unset();
        if (this->unit->heavy) {
            this->factory->heavy_relocate_count--;
            this->target_factory->inbound_heavy_relocate_count--;
        } else {
            this->factory->light_relocate_count--;
            this->target_factory->inbound_light_relocate_count--;
        }
    }
}

void RoleRelocate::teardown() {
}

bool RoleRelocate::is_valid() {
    if (!this->target_factory->alive()) return false;

    // If source factory explodes, skip ahead and update assignment and invalidate
    if (!this->factory->alive()) {
        this->unit->update_assigned_factory(this->target_factory);
        return false;
    }

    // After reaching target factory, update assignment and invalidate
    if (this->goal == this->target_factory
        && this->unit->cell()->man_dist_factory(this->target_factory) <= 1) {
        this->unit->update_assigned_factory(this->target_factory);
        return false;
    }

    return true;
}

Cell *RoleRelocate::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->factory->cell) return this->target_factory->cell;

    // Goal is target_factory
    if (this->goal == this->target_factory) return this->target_factory->cell;

    // Goal is factory
    return this->factory->cell;
}

void RoleRelocate::update_goal() {
    if (this->goal == this->target_factory) {  // Done with target factory goal?
        // One way ticket
    } else if (this->goal == this->factory) {  // Done with factory goal?
        int power_threshold = (
            + 3 * board.naive_cost(this->unit, this->unit->cell(), this->target_factory->cell) / 2);
        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold) {
            this->goal = this->target_factory;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleRelocate::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleRelocate::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleRelocate::do_move B");
    return false;
}

bool RoleRelocate::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleRelocate::do_dig A");
    return false;
}

bool RoleRelocate::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleRelocate::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleRelocate::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleRelocate::do_pickup A");
    int power_threshold = (
        + 3 * board.naive_cost(this->unit, this->unit->cell(), this->target_factory->cell) / 2);
    power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);

    return this->_do_power_pickup(/*max_amount*/power_threshold+1);
}
