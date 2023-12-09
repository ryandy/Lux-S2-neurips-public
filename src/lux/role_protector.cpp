#include "lux/role_protector.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_recharge.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleProtector::RoleProtector(Unit *_unit, Cell *_factory_cell, Unit *_miner_unit)
    : Role(_unit, 'f'), factory_cell(_factory_cell), miner_unit(_miner_unit)
{
    this->goal = this->get_factory();
    this->last_strike_step = INT_MIN;

    this->_is_protecting_step = -1;
    this->_should_strike_step = -1;
}

bool RoleProtector::from_transition_protect_ice_miner(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RoleProtector::from_transition_protect_ice_miner A");

    if (!board.sim0() || !_unit->heavy) return false;

    // Some roles/situations should not transition
    Cell *cur_cell = _unit->cell();
    if (_unit->role
        && (false
            || (RoleAntagonizer::cast(_unit->role)
                && RoleAntagonizer::cast(_unit->role)->can_destroy_factory())
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack)
            || (RoleCow::cast(_unit->role)
                && RoleCow::cast(_unit->role)->repair)
            || (RoleMiner::cast(_unit->role)
                && RoleMiner::cast(_unit->role)->resource_cell->ice
                && !_unit->antagonizer_unit)
            || RolePincer::cast(_unit->role)
            || RoleProtector::cast(_unit->role)
            || RoleRecharge::cast(_unit->role))) {
        return false;
    }

    Unit *best_unit = NULL;
    int min_dist = INT_MAX;
    Factory *factory = _unit->assigned_factory;
    RoleMiner *role_miner;
    for (Unit *miner_unit : factory->heavies) {
        if (miner_unit != _unit
            && miner_unit->antagonizer_unit
            && (role_miner = RoleMiner::cast(miner_unit->role))
            && !role_miner->protector
            && role_miner->resource_cell->ice
            && role_miner->resource_cell->man_dist_factory(factory) == 1
            && role_miner->resource_cell->man_dist(miner_unit->cell()) < 2) {
            int dist = role_miner->resource_cell->man_dist(cur_cell);
            if (dist < min_dist) {
                min_dist = dist;
                best_unit = miner_unit;
            }
        }
    }

    if (best_unit) {
        RoleMiner *role_miner = RoleMiner::cast(best_unit->role);
        LUX_ASSERT(role_miner);

        if (role_miner->power_transporter) role_miner->power_transporter->delete_role();

        Cell *factory_cell = role_miner->resource_cell->neighbor_toward(factory->cell);
        LUX_ASSERT(factory_cell->factory);
        Role::_displace_unit(factory_cell);

        *new_role = new RoleProtector(_unit, factory_cell, best_unit);
        return true;
    }

    return false;
}

bool RoleProtector::from_transition_power_transporter(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RoleProtector::from_transition_power_transporter A");

    RolePowerTransporter *role_pt;
    if (!board.sim0()
        || !_unit->heavy
        || !(role_pt = RolePowerTransporter::cast(_unit->role))) {
        return false;
    }

    Factory *factory = role_pt->get_factory();
    Unit *miner_unit = role_pt->target_unit;
    RoleMiner *role_miner = RoleMiner::cast(miner_unit->role);
    LUX_ASSERT(role_miner);
    LUX_ASSERT(role_miner->resource_cell->ice);
    LUX_ASSERT(role_miner->resource_cell->man_dist_factory(factory) == 1);
    LUX_ASSERT(role_miner->resource_cell->man_dist(role_pt->factory_cell) == 1);

    if (miner_unit->cell()->man_dist(role_miner->resource_cell) < 2
        && RoleProtector::threat_units(miner_unit, /*steps*/1, /*radius*/2)) {
        *new_role = new RoleProtector(_unit, role_pt->factory_cell, miner_unit);
        return true;
    }

    return false;
}

bool RoleProtector::threat_units(Unit *miner_unit, int past_steps, int max_radius,
                                 vector<Unit*> *threat_units) {
    LUX_ASSERT(miner_unit);
    RoleMiner *role_miner = RoleMiner::cast(miner_unit->role);
    LUX_ASSERT(role_miner);
    return miner_unit->threat_units(role_miner->resource_cell, past_steps, max_radius,
                                    /*ignore_heavies*/false, /*ignore_lights*/false, threat_units);
}

int RoleProtector::threat_power(int past_steps, int max_radius) {
    vector<Unit*> threat_units;
    (void)RoleProtector::threat_units(this->miner_unit, past_steps, max_radius, &threat_units);
    int threat_power = 0;
    for (Unit *threat_unit : threat_units) {
        threat_power = MAX(threat_power, threat_unit->power);
    }
    return threat_power;
}

bool RoleProtector::in_position() {
    RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
    LUX_ASSERT(role_miner);

    // We consider the units to be in position even if the miner is off by 1. This is so that they
    // can perform a protected move to get back to the resource cell.
    return (this->unit->cell() == this->factory_cell
            && this->miner_unit->cell()->man_dist(role_miner->resource_cell) <= 1);
}

bool RoleProtector::is_protecting() {
    if (board.step != this->_is_protecting_step) {
        this->_is_protecting_step = board.step;
        this->_is_protecting = (
            board.sim0()
            && this->goal_type == 'c'
            && this->in_position()
            && (this->miner_unit->power
                >= this->miner_unit->cfg->DIG_COST + this->miner_unit->cfg->ACTION_QUEUE_POWER_COST)
            && this->unit->power - this->unit->cfg->ACTION_QUEUE_POWER_COST > this->threat_power(1,1));
        if (this->unit->_log_cond()) LUX_LOG(*this->unit<< " is_protecting? " << this->_is_protecting);
    }
    return this->_is_protecting;
}

// Assumes is_protecting is true
bool RoleProtector::should_strike() {
    if (board.step != this->_should_strike_step) {
        this->_should_strike_step = board.step;
        this->_should_strike = false;

        RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
        LUX_ASSERT(role_miner);

        vector<Unit*> threat_units;
        (void)RoleProtector::threat_units(this->miner_unit, 1, 1, &threat_units);
        if (threat_units.size() == 1
            && threat_units[0]->cell() == role_miner->resource_cell
            && this->miner_unit->cell()->man_dist(role_miner->resource_cell) == 1) {
            // Possible that only threat is standing on resource cell and miner is off by 1
            // In this case we can just let the miner move onto the resource cell
            // This is 100% safe, not even a "protected move"
        } else if (!threat_units.empty()
                   && this->get_factory()->total_water() > 5
                   && (this->last_strike_step <= board.step - 10
                       || prandom(board.sim_step + this->unit->id, PROTECTOR_STRIKE_CHANCE))) {
            this->_should_strike = true;
            this->last_strike_step = board.step;
        }
        if (this->unit->_log_cond()) LUX_LOG(*this->unit<< " should_strike? " << this->_should_strike);
    }

    return this->_should_strike;
}

bool RoleProtector::is_striking() {
    return this->is_protecting() && this->should_strike();
}

bool RoleProtector::is_covering() {
    return this->is_protecting() && !this->should_strike();
}

void RoleProtector::print(ostream &os) const {
    if (!this->factory_cell->factory) {
        os << "Protector[N/A]";
        return;
    }

    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    os << "Protector[" << *this->factory_cell->factory << fgoal << " -> "
       << *this->miner_unit << cgoal << "]";
}

Factory *RoleProtector::get_factory() {
    return this->factory_cell->factory;
}

double RoleProtector::power_usage() {
    return this->unit->cfg->ACTION_QUEUE_POWER_COST + this->unit->cfg->MOVE_COST / 2.0;
}

void RoleProtector::set() {
    Role::set();
    this->factory_cell->set_unit_assignment(this->unit);
    RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
    role_miner->set_protector(this->unit);
}

void RoleProtector::unset() {
    if (this->is_set()) {
        this->factory_cell->unset_unit_assignment(this->unit);
        RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
        if (role_miner) role_miner->unset_protector();
        Role::unset();
    }
}

void RoleProtector::teardown() {
}

bool RoleProtector::is_valid() {
    RoleMiner *role_miner;
    bool is_valid = (this->get_factory()
                     && this->get_factory()->alive()
                     && this->miner_unit->alive()
                     && (role_miner = RoleMiner::cast(this->miner_unit->role))
                     && role_miner->resource_cell->man_dist(this->factory_cell) == 1);
    return is_valid;
}

Cell *RoleProtector::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->get_factory()->cell) return this->factory_cell;

    // Goal is assigned factory cell
    if (this->goal_type == 'c') {
        return this->factory_cell;
    }

    // Goal is factory
    return this->get_factory()->cell;
}

void RoleProtector::update_goal() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::update_goal A");
    int threat_power = MIN(2989, this->threat_power());

    if (this->goal_type == 'c') {  // Done with unit protection goal?
        if (this->unit->power <= threat_power) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::update_goal B");
            this->goal_type = 'f';
            this->goal = this->get_factory();
        }
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        if (this->unit->power > threat_power) {
            //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::update_goal C");
            this->goal_type = 'c';
            this->goal = this->factory_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
    //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::update_goal D");
}

bool RoleProtector::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_move A");
        return true;
    }

    // We are at factory cell
    // For step idx0, either strike out at resource_cell, or sit tight and pickup/transfer
    // For all future steps, we will claim that we _plan_ on striking resource cell
    if ((board.sim0() && this->is_striking())
        || (!board.sim0() && this->in_position())) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_move B");
        RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
        LUX_ASSERT(role_miner);
        if (this->_do_move(role_miner->resource_cell)) {
            if (this->unit->_log_cond()) LUX_LOG(*this->unit << " protector strike");
            return true;
        }
    }

    // If in position on idx0 and we are not striking, picking up, or transferring, stand still
    if (board.sim0() && this->in_position()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_move D");
        return this->_do_no_move();
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_move E");
    return false;
}

bool RoleProtector::do_dig() {
    return false;
}

bool RoleProtector::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_transfer A");
    if (this->goal_type == 'f') {
        return this->_do_transfer_resource_to_factory();
    }

    // Only actually do pickup on step idx0
    if (!board.sim0()) {
        return false;
    }

    // Only transfer from factory cell
    if (this->unit->cell() != this->factory_cell) {
        return false;
    }

    // No transfer if striking this step
    if (this->is_striking()) {
        return false;
    }

    RoleMiner *role_miner = RoleMiner::cast(this->miner_unit->role);
    LUX_ASSERT(role_miner);

    // Need miner unit to already have move to resource cell figured out
    if (this->miner_unit->cell_next() != role_miner->resource_cell) {
        return false;
    }

    int miner_power = this->miner_unit->power;
    int digs_remaining = (miner_power / (this->miner_unit->cfg->ACTION_QUEUE_POWER_COST
                                         + this->miner_unit->cfg->DIG_COST));
    int power_gain = this->miner_unit->power_gain(board.step, board.step + digs_remaining);
    digs_remaining = ((miner_power + power_gain) / (this->miner_unit->cfg->ACTION_QUEUE_POWER_COST
                                                    + this->miner_unit->cfg->DIG_COST));
    if (digs_remaining >= 10) {
        return false;
    }

    int protector_power = this->unit->power;
    int threat_power = this->threat_power();
    int protector_power_to_keep = threat_power + 100;
    if (protector_power <= protector_power_to_keep) {
        return false;
    }

    int amount = (this->miner_unit->cfg->BATTERY_CAPACITY
                  - miner_power
                  - this->miner_unit->power_gain(board.step));
    amount = MIN(amount, protector_power - protector_power_to_keep);

    int max_miner_power = MAX(threat_power + 100,
                              20 * (this->miner_unit->cfg->ACTION_QUEUE_POWER_COST
                                    + this->miner_unit->cfg->DIG_COST));
    max_miner_power = MIN(max_miner_power, this->miner_unit->cfg->BATTERY_CAPACITY);
    int max_amount = max_miner_power - miner_power;
    amount = MIN(amount, max_amount);
    amount = (amount / 100) * 100;  // round down to nearest 100

    if (amount > 0
        && (this->unit->power
            >= this->unit->transfer_cost(role_miner->resource_cell, Resource_POWER, amount))) {
        this->unit->do_transfer(role_miner->resource_cell, Resource_POWER, amount);
        return true;
    }

    return false;
}

bool RoleProtector::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleProtector::do_pickup A");
    if (this->goal_type == 'f') {
        return this->_do_power_pickup();
    }

    // Only actually do pickup on step idx0
    if (!board.sim0()) {
        return false;
    }

    // Only pickup at factory cell
    if (this->unit->cell() != this->factory_cell) {
        return false;
    }

    // No pickup if striking this step
    if (this->is_striking()) {
        return false;
    }

    int protector_power = this->unit->power;
    int miner_power = this->miner_unit->power;
    int threat_power = this->threat_power();

    int max_miner_power = MAX(threat_power + 100,
                              20 * (this->miner_unit->cfg->ACTION_QUEUE_POWER_COST
                                    + this->miner_unit->cfg->DIG_COST));
    max_miner_power = MIN(max_miner_power, this->miner_unit->cfg->BATTERY_CAPACITY);
    int power_for_miner = MAX(0, max_miner_power - miner_power);
    int power_for_protector = MAX(0, threat_power + 100 - protector_power);
    if (power_for_protector == 0) {
        int protector_surplus = protector_power - (threat_power + 100);
        power_for_miner = MAX(0, power_for_miner - protector_surplus);
    }

    int power_needed = power_for_miner + power_for_protector;
    power_needed = ((power_needed + 99) / 100) * 100;  // round up to nearest 100

    if (power_needed) {
        return this->_do_power_pickup(power_needed);
    }

    return false;
}
