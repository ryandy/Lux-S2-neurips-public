#include "lux/role_defender.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/role_recharge.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleDefender::RoleDefender(Unit *_unit, Factory *_factory, Cell *_target_cell)
    : Role(_unit), factory(_factory), target_cell(_target_cell)
{
    int fdist = _unit->cell()->man_dist_factory(_factory);
    int cdist = _unit->cell()->man_dist(_target_cell);
    if (fdist < cdist) {
        this->goal_type = 'f';
        this->goal = _factory;
    } else {
        this->goal_type = 'c';
        this->goal = _target_cell;
    }
}

bool RoleDefender::from_unit(Role **new_role, Unit *_unit, int max_dist) {
    //if (_unit->_log_cond()) LUX_LOG("RoleDefender::from_unit A");

    Factory *factory = _unit->assigned_factory;

    Cell *best_cell = NULL;
    int best_score = INT_MIN;

    // First try to defend current lichen territory
    for (Cell *cell : factory->lichen_growth_cells) {
        if (!cell->assigned_unit
            && cell->home_dist > 1
            && cell->away_dist > 1
            && (factory->cell->away_dist < 30
                || cell->away_dist < factory->cell->away_dist)
            && cell->x % 2 == 0
            && cell->y % 2 == 0
            && (!cell->north || !cell->north->assigned_unit)
            && (!cell->east  || !cell->east->assigned_unit)
            && (!cell->south || !cell->south->assigned_unit)
            && (!cell->west  || !cell->west->assigned_unit)) {
            // Want high dist from factory, low dist to opp factories
            int dist = cell->man_dist_factory(factory);
            int score = dist - 2 * cell->away_dist;
            if (_unit->cell()->man_dist(cell) <= 1) score += 10;
            if (dist <= max_dist && score > best_score) {
                best_cell = cell;
                best_score = score;
            }
        }
    }

    if (best_cell) {
        *new_role = new RoleDefender(_unit, factory, best_cell);
        return true;
    }

    // Second try to station in a low-rubble area between our factory and theirs
    max_dist = MAX(max_dist, 2 * factory->cell->away_dist / 3);
    Factory *nearest_opp_factory = factory->cell->nearest_factory(board.opp);
    Cell *mid_cell = board.cell((factory->x + nearest_opp_factory->x) / 2,
                                (factory->y + nearest_opp_factory->y) / 2);
    Cell *cell = mid_cell->radius_cell(SIZE / 2);
    while (cell) {
        if (cell->rubble < 40
            && !cell->assigned_unit
            && cell->home_dist > 1
            && cell->away_dist > 1
            && cell->x % 2 == 0
            && cell->y % 2 == 0
            && (!cell->north || !cell->north->assigned_unit)
            && (!cell->east  || !cell->east->assigned_unit)
            && (!cell->south || !cell->south->assigned_unit)
            && (!cell->west  || !cell->west->assigned_unit)
            && cell->man_dist_factory(factory) <= max_dist) {
            *new_role = new RoleDefender(_unit, factory, cell);
            return true;
        }
        cell = mid_cell->radius_cell(SIZE / 4, cell);
    }

    return false;
}

void RoleDefender::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    os << "Defender[" << *this->factory << fgoal << " -> "
       << *this->target_cell << cgoal << "]";
}

Factory *RoleDefender::get_factory() {
    return this->factory;
}

double RoleDefender::power_usage() {
    if (this->unit->cell()->factory == this->factory) return this->unit->cfg->CHARGE;
    return 0;
}

void RoleDefender::set() {
    Role::set();
    this->target_cell->set_unit_assignment(this->unit);
    // TODO: idle_count ?
    //if (this->unit->heavy) this->factory->heavy_relocate_count++;
    //else this->factory->light_relocate_count++;
}

void RoleDefender::unset() {
    if (this->is_set()) {
        this->target_cell->unset_unit_assignment(this->unit);
        //if (this->unit->heavy) this->factory->heavy_relocate_count--;
        //else this->factory->light_relocate_count--;
        Role::unset();
    }
}

void RoleDefender::teardown() {
}

bool RoleDefender::is_valid() {
    bool is_valid = (this->factory->alive()
                     && this->unit->power < this->unit->cfg->BATTERY_CAPACITY
                     && (!this->target_cell->north || !this->target_cell->north->assigned_unit)
                     && (!this->target_cell->east  || !this->target_cell->east->assigned_unit)
                     && (!this->target_cell->south || !this->target_cell->south->assigned_unit)
                     && (!this->target_cell->west  || !this->target_cell->west->assigned_unit));

    return is_valid;
}

Cell *RoleDefender::goal_cell() {
    // Override goal if on factory center
    if (this->unit->cell() == this->factory->cell) return this->target_cell;

    // Goal is target cell
    if (this->goal_type == 'c') {
        // No need to push friend
        if (this->unit->cell()->man_dist(this->target_cell) == 1
            && this->target_cell->unit
            && !this->target_cell->unit->cell_next()) {
            return this->unit->cell();
        }
        return this->target_cell;
    }

    // Goal is factory
    return this->factory->cell;
}

void RoleDefender::update_goal() {
    if (this->goal_type == 'c') {  // Done with target cell goal?
        // Handled by RoleRecharge
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int unit_resource = MAX(this->unit->ice, this->unit->ore);
        int power_threshold = (
            this->unit->cfg->ACTION_QUEUE_POWER_COST
            + 3 * this->unit->cfg->MOVE_COST
            + 6 * this->unit->cfg->MOVE_COST);
        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold && unit_resource == 0) {
            this->goal_type = 'c';
            this->goal = this->target_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleDefender::do_move() {
    int power_threshold = (
        this->unit->cfg->ACTION_QUEUE_POWER_COST
        + 3 * this->unit->cfg->MOVE_COST
        + 6 * this->unit->cfg->MOVE_COST
        + board.naive_cost(this->unit, this->unit->cell(), this->factory->cell));
    if (this->unit->power >= power_threshold) {
        return this->_do_move();
    }
    return false;
}

bool RoleDefender::do_dig() {
    // Do very small touchup jobs if near target
    Cell *cur_cell = this->unit->cell();
    if (this->goal_type == 'c'
        && cur_cell->rubble > 0
        && cur_cell->rubble <= this->unit->cfg->DIG_RUBBLE_REMOVED
        && cur_cell->man_dist(this->target_cell) <= 1) {
        if (this->unit->power >= this->unit->dig_cost()) {
            this->unit->do_dig();
            return true;
        }
    }
    return false;
}

bool RoleDefender::do_transfer() {
    Cell *cur_cell = this->unit->cell();

    // Transfer power to ally if possible
    if (this->goal_type == 'c') {
        for (Cell *neighbor : cur_cell->neighbors) {
            if (neighbor->unit_next
                && RoleRecharge::cast(neighbor->unit_next->role)) {
                int amount = (
                    neighbor->unit_next->cfg->BATTERY_CAPACITY
                    - neighbor->unit_next->power
                    - neighbor->unit_next->power_delta
                    - neighbor->unit_next->power_gain());
                int power_to_keep = (
                    this->unit->cfg->ACTION_QUEUE_POWER_COST
                    + 3 * this->unit->cfg->MOVE_COST
                    + 3 * this->unit->cfg->MOVE_COST * cur_cell->man_dist_factory(this->factory));
                amount = MIN(amount, this->unit->power - power_to_keep);
                if (amount > 0) {
                    if (this->unit->power
                        >= this->unit->transfer_cost(neighbor, Resource_POWER, amount)) {
                        //if (board.sim0()) LUX_LOG(*this->unit << " def tx " << *neighbor->unit_next);
                        this->unit->do_transfer(neighbor, Resource_POWER, amount);
                        return true;
                    }
                }
            }
        }
    }

    return this->_do_transfer_resource_to_factory();
}

bool RoleDefender::do_pickup() {
    return this->_do_power_pickup();
}
