#include "lux/role_water_transporter.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_blockade.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleWaterTransporter::RoleWaterTransporter(Unit *_unit, Factory *_factory, Factory *_target_factory)
    : Role(_unit, 'f'), factory(_factory), target_factory(_target_factory)
{
    this->goal = _factory;
}

bool RoleWaterTransporter::from_ice_conflict(Role **new_role, Unit *_unit,
                                             int water_threshold, int max_count) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RoleWaterTransporter::from_ice_conflict A");

    Factory *factory = _unit->assigned_factory;
    LUX_ASSERT(ModeIceConflict::cast(factory->mode));
    int factory_water = factory->total_water();
    if (factory_water >= water_threshold) return false;

    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RoleWaterTransporter::cast(r); }) >= max_count) return false;

    Factory *best_factory = NULL;
    double best_score = INT_MIN;
    for (Factory *other_factory : board.player->factories()) {
        if (other_factory == factory
            || ModeIceConflict::cast(other_factory->mode)) continue;

        int dist = factory->cell->man_dist_factory(other_factory);
        int water = other_factory->total_water();
        double water_income = other_factory->water_income();
        double score = (20 * water_income
                        + MAX(0, water - 150)  // bonus for high water
                        - dist
                        - 100 * other_factory->inbound_water_transporter_count);

        if (board.low_iceland()
            && water < 200) continue;

        if (2 * dist > factory_water) {  // probably won't make it in time
            score -= 1000;
        }
        if (2 * dist + 50 > factory_water) {  // cutting it close
            score -= (2 * dist + 50 - factory_water) * 3;
        }

        if (score > best_score) {
            best_score = score;
            best_factory = other_factory;
        }
    }

    if (best_factory) {
        *new_role = new RoleWaterTransporter(_unit, factory, best_factory);
        return true;
    }

    return false;
}

bool RoleWaterTransporter::from_transition_ice_conflict(Role **new_role, Unit *_unit,
                                                        int water_threshold, int max_count) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RoleWaterTransporter::from_transition_ice_conflict A");

    Factory *factory = _unit->assigned_factory;
    if (!ModeIceConflict::cast(factory->mode)) return false;

    // Various roles/situations should not transition
    if (_unit->role
        && (false
            || RoleBlockade::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    return RoleWaterTransporter::from_ice_conflict(new_role, _unit, water_threshold, max_count);
}

void RoleWaterTransporter::print(ostream &os) const {
    string fgoal = this->goal == this->factory ? "*" : "";
    string tgoal = this->goal == this->target_factory ? "*" : "";
    os << "WaterTransporter[" << *this->factory << fgoal << " -> "
       << *this->target_factory << tgoal << "]";
}

Factory *RoleWaterTransporter::get_factory() {
    return this->factory;
}

double RoleWaterTransporter::power_usage() {
    // Note: power cost is effectively split between the two factories
    return 2 * this->unit->cfg->MOVE_COST;
}

void RoleWaterTransporter::set() {
    Role::set();
    this->target_factory->inbound_water_transporter_count++;
}

void RoleWaterTransporter::unset() {
    if (this->is_set()) {
        this->target_factory->inbound_water_transporter_count--;
        Role::unset();
    }
}

void RoleWaterTransporter::teardown() {
}

bool RoleWaterTransporter::is_valid() {
    bool is_valid = (this->factory->alive()
                     // target factory alive and well
                     && ((this->target_factory->alive()
                          && !ModeIceConflict::cast(this->target_factory->mode))
                         // or already headed back to home factory
                         || (this->unit->water
                             && this->goal == this->factory)));

    // After returning to home factory, check if still needed
    if (is_valid
        && this->goal == this->factory
        && this->unit->ice == 0
        && this->unit->water == 0
        && (!ModeIceConflict::cast(this->factory->mode)
            || this->factory->total_water() >= 130)) {
        is_valid = false;
    }

    return is_valid;
}

Cell *RoleWaterTransporter::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->factory->cell) return this->target_factory->cell;

    // Goal is target factory
    if (this->goal == this->target_factory) return this->target_factory->cell;

    // Goal is factory
    return this->factory->cell;
}

void RoleWaterTransporter::update_goal() {
    int unit_water = this->unit->water + this->unit->ice / ICE_WATER_RATIO;

    if (this->goal == this->target_factory) {  // Done with target factory goal?
        int water_threshold = 3 * this->factory->cell->man_dist_factory(this->target_factory) / 2;
        water_threshold = MAX(10, water_threshold);
        water_threshold = MIN(100, water_threshold);
        if (unit_water >= water_threshold) {
            int power_threshold = 2 * board.naive_cost(
                this->unit, this->unit->cell(), this->factory->cell);
            power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
            if (this->unit->power >= power_threshold) {
                this->goal = this->factory;
            }
        }
    } else if (this->goal == this->factory) {  // Done with factory goal?
        if (unit_water == 0) {
            int power_threshold = 2 * board.naive_cost(
                this->unit, this->unit->cell(), this->target_factory->cell);
            power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
            if (this->unit->power >= power_threshold) {
                this->goal = this->target_factory;
            }
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleWaterTransporter::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_move B");
    return false;
}

bool RoleWaterTransporter::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_dig A");
    return false;
}

bool RoleWaterTransporter::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleWaterTransporter::_do_water_pickup() {
    if (this->goal != this->target_factory) return false;
    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_water_pickup A");

    Cell *cur_cell = this->unit->cell();
    Factory *cur_factory = cur_cell->factory;
    if (cur_cell->factory_center
        || !cur_factory
        || cur_factory != this->target_factory) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_water_pickup B");
        return false;
    }

    int unit_water = this->unit->water + this->unit->ice / ICE_WATER_RATIO;
    int water_threshold = 3 * this->factory->cell->man_dist_factory(this->target_factory) / 2;
    water_threshold = MAX(10, water_threshold);
    water_threshold = MIN(100, water_threshold);
    if (unit_water >= water_threshold) return false;

    int water_amount = water_threshold - unit_water;
    int ice_amount = water_amount * ICE_WATER_RATIO;
    if (cur_factory->ice + cur_factory->ice_delta >= ice_amount
        && ice_amount > 0
        && ice_amount <= this->unit->cfg->CARGO_SPACE
        && this->unit->water == 0
        && this->unit->power >= this->unit->pickup_cost(Resource_ICE, ice_amount)) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_water_pickup C");
        this->unit->do_pickup(Resource_ICE, ice_amount);
        return true;
    }

    water_amount = MIN(water_amount, cur_factory->water + cur_factory->water_delta - 30);
    if (water_amount > 0
        && this->unit->power >= this->unit->pickup_cost(Resource_WATER, water_amount)) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_water_pickup D");
        this->unit->do_pickup(Resource_WATER, water_amount);
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_water_pickup E");
    return false;
}

bool RoleWaterTransporter::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_pickup A");
    if (this->_do_water_pickup()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_pickup B");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleWaterTransporter::do_pickup C");
    Factory *alt_factory = (this->goal == this->target_factory ? this->target_factory : NULL);
    return this->_do_power_pickup(/*max*/INT_MAX, /*override*/false, /*alt*/alt_factory);
}
