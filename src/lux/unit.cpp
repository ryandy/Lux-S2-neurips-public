#include "lux/unit.hpp"

#include <set>

#include "lux/action.hpp"
#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_defender.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pillager.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_water_transporter.hpp"
using namespace std;


bool Unit::_log_cond() {
    return (
        true
        && false
        && board.sim0()
        //&& this->id == 32
        && RoleBlockade::cast(this->role)
        );
}


UnitConfig g_light_cfg = {
    .ACTION_QUEUE_POWER_COST = 1,
    .BATTERY_CAPACITY = 150,
    .CARGO_SPACE = 100,
    .CHARGE = 1,
    .DIG_COST = 5,
    .DIG_LICHEN_REMOVED = 10,
    .DIG_RESOURCE_GAIN = 2,
    .DIG_RUBBLE_REMOVED = 2,
    .INIT_POWER = 50,
    .METAL_COST = 10,
    .MOVE_COST = 1,
    .POWER_COST = 50,
    .RUBBLE_AFTER_DESTRUCTION = 1,
    .RUBBLE_MOVEMENT_COST = 0.05,
    .SELF_DESTRUCT_COST = 10,
    .RAZE_COST = 10 + 1
};

UnitConfig g_heavy_cfg = {
    .ACTION_QUEUE_POWER_COST = 10,
    .BATTERY_CAPACITY = 3000,
    .CARGO_SPACE = 1000,
    .CHARGE = 10,
    .DIG_COST = 60,
    .DIG_LICHEN_REMOVED = 100,
    .DIG_RESOURCE_GAIN = 20,
    .DIG_RUBBLE_REMOVED = 20,
    .INIT_POWER = 500,
    .METAL_COST = 100,
    .MOVE_COST = 20,
    .POWER_COST = 500,
    .RUBBLE_AFTER_DESTRUCTION = 10,
    .RUBBLE_MOVEMENT_COST = 1,
    .SELF_DESTRUCT_COST = 100,
    .RAZE_COST = 60 + 10
};

void Unit::init(int unit_id, int _player_id, int _x, int _y, bool _heavy, int step,
		int _ice, int _ore, int _water, int _metal, int _power, json *aq_json) {
    // Init once:
    if (this->build_step == 0) {
	this->build_step = step;
	this->id = unit_id;
	this->player = board.get_player(_player_id);
	this->heavy = _heavy;
	this->cfg = _heavy ? &g_heavy_cfg : &g_light_cfg;
        this->role = NULL;
        this->_save_role = NULL;
        this->assigned_unit = NULL;
        this->assigned_factory = NULL;
        this->action_queue_update_count = 0;
        this->prev_ice = 0;
        this->prev_ore = 0;
        this->prev_water = 0;
        this->prev_prev_water = 0;
        this->prev_rubble = 0;
        this->prev_lichen_strain = -1;
    }

    // Re-init every step:
    this->x = _x;
    this->y = _y;
    this->ice = _ice;
    this->ore = _ore;
    this->water = _water;
    this->metal = _metal;
    this->power = _power;
    this->power_init = _power;
    this->alive_step = step;
    this->last_action_step = -1;
    this->action_queue_cost_step = INT_MAX;
    this->action_queue_cost_iou = false;

    // Register unit with cell
    if (step > board.sim_step) {
        // Only executed for future units:
        this->register_move(Direction_CENTER);  // sets unit_next
    } else {
        // Not executed for future units:
        this->cell()->unit = this;
        this->cell_history.push_back(this->cell());
        // Note: cell.unit_history updated in Board::begin_step
    }

    // Process given action queue
    this->raq_len = 0;
    this->aq_len = 0;
    new_action_queue.clear();

    // Future units have no prior json AQ
    if (aq_json) {
        for (auto &a_json : *aq_json) {
            ActionSpec *spec = &this->raw_action_queue[this->raq_len++];
            spec->action = a_json.at(0);
            spec->direction = a_json.at(1);
            spec->resource = a_json.at(2);
            spec->amount = a_json.at(3);
            spec->repeat = a_json.at(4);
            spec->n = a_json.at(5);
        }
        this->expand_raw_action_queue();
    }
}

void Unit::save_end() {
    if (this->build_step > board.sim_step) return;  // Ignore future units created this sim step
    this->_save_role = this->role->copy();
    this->_save_route = this->route;
    this->_save_assigned_factory = this->assigned_factory;
    //if (this->_log_cond()) LUX_LOG("Do " << *this << ' ' << this->action);
}

void Unit::load() {
    if (this->role) delete this->role;
    this->role = this->_save_role;
    this->route = this->_save_route;
    this->assigned_factory = this->_save_assigned_factory;
}

void Unit::handle_destruction() {
    if (this->role) {
        LUX_LOG("died: " << *this << ' ' << *this->role << ' ' << *this->cell());
    } else {
        LUX_ASSERT(this->player == board.opp);
        LUX_LOG("died: " << *this << ' ' << *this->cell());
    }
}

bool Unit::alive() {
    return this->alive_step >= board.step;
}

string Unit::id_str() {
    return "unit_" + to_string(this->id);
}

Cell *Unit::cell() {
    return board.cell(this->x, this->y);
}

Cell *Unit::cell_next() {
    if (this->x_delta == INT16_MAX) return NULL;
    return board.cell(this->x + this->x_delta, this->y + this->y_delta);
}

Cell *Unit::cell_at(int step) {
    LUX_ASSERT(step <= board.step);
    if (step < this->build_step) return NULL;  // unborn
    int idx = step - this->build_step;
    if (!this->alive() && idx >= this->cell_history.size()) return NULL;  // dead
    /*if (idx < 0 || idx >= this->cell_history.size()) {
        LUX_LOG("Warning: " << *this
                << " idx=" << idx
                << " size=" << this->cell_history.size()
                << " step=" << step
                << " bstep=" << board.step
                << " build_step=" << this->build_step);
    }*/
    LUX_ASSERT(idx >= 0);
    LUX_ASSERT(idx < this->cell_history.size());
    return this->cell_history[step - this->build_step];
}

void Unit::update_stats_begin() {
    LUX_ASSERT(board.step == board.sim_step);  // Only called beginning of first sim step

    if (!this->alive()) return;

    // Update last_factory for all units
    Cell *cur_cell = this->cell();
    if (cur_cell->factory) {
        this->last_factory = cur_cell->factory;  // must be before f.update_units
    } else if (!this->last_factory  // created same step as factory destroyed
               || !this->last_factory->alive()) {
        this->last_factory = cur_cell->nearest_factory(this->player);
    }

    // Own units only:
    if (this->player == board.player) {
        this->update_antagonizer_unit();
    }

    // Opp units only:
    if (this->player == board.opp) {
        // Calculate low_power info for opp units
        this->update_low_power();
        this->update_is_trapped();

        // Monitor digging: mining, cowing, and pillaging
        if (!this->cell_history.empty() && cur_cell == this->cell_history.back()) {
            if ((cur_cell->ice && this->ice > this->prev_ice)
                || (cur_cell->ore && this->ore > this->prev_ore)
                || ((cur_cell->ice || cur_cell->ore) && cur_cell->rubble < this->prev_rubble)) {
                // Save to unit and board
                this->mine_cell_steps.push_back(make_pair(cur_cell, board.step - 1));
                auto &board_cell_steps = (this->heavy
                                          ? board.heavy_mine_cell_steps
                                          : board.light_mine_cell_steps);
                board_cell_steps.push_back(make_pair(cur_cell, board.step - 1));
            }
            else if (cur_cell->rubble < this->prev_rubble
                     && !cur_cell->ice
                     && !cur_cell->ore) {
                // Save to board
                auto &board_cell_steps = (this->heavy
                                           ? board.heavy_cow_cell_steps
                                           : board.light_cow_cell_steps);
                board_cell_steps.push_back(make_pair(cur_cell, board.step - 1));
            }
            else if (this->prev_rubble == 0
                     && cur_cell->rubble > 0
                     && this->prev_lichen_strain != -1
                     && cur_cell->lichen_strain == -1) {
                // Save to factory
                Factory *factory = &board.factories[this->prev_lichen_strain];
                if (factory->alive()) {
                    factory->pillage_cell_steps.push_back(make_pair(cur_cell, board.step - 1));
                }
            }
        }
    }

    this->prev_ice = this->ice;
    this->prev_ore = this->ore;
    this->prev_prev_water = this->prev_water;
    this->prev_water = this->water;
    this->prev_rubble = cur_cell->rubble;
    this->prev_lichen_strain = cur_cell->lichen_strain;
}

void Unit::update_assigned_factory(Factory *new_factory) {
    if (!new_factory) new_factory = this->role->get_factory();
    if (new_factory != this->assigned_factory) {
        this->assigned_factory->remove_unit(this);
        this->assigned_factory = new_factory;
        this->assigned_factory->add_unit(this);
    }
}

void Unit::update_antagonizer_unit() {
    this->antagonizer_unit = NULL;

    int max_count = 0;
    set<Unit*> checked;
    for (int i = (int)this->threat_unit_steps.size() - 1; i >= 0; i--) {
        Unit *threat_unit = this->threat_unit_steps[i].first;
        if (!threat_unit->alive()) continue;
        int recent_step = this->threat_unit_steps[i].second;
        if (recent_step < board.step - 2) break;
        if (checked.count(threat_unit)) continue;
        checked.insert(threat_unit);

        int count = 1;
        for (int j = i - 1; j >= 0; j--) {
            Unit *past_threat_unit = this->threat_unit_steps[j].first;
            int past_step = this->threat_unit_steps[j].second;
            if (past_step < board.step - 6) break;
            // TODO: Require that threat_unit be in same/similar cell each time?
            //       otherwise e.g. an opp heavy moving past/through a unit can trigger this
            if (past_threat_unit == threat_unit) count++;
        }

        if (count > max_count && count >= 3) {
            max_count = count;
            this->antagonizer_unit = threat_unit;
            //if (this->_log_cond()) LUX_LOG(*this << " antagonized by " << *threat_unit);

            // Check to see if opp unit is oscillating
            if (this->heavy == threat_unit->heavy
                && threat_unit->cell_at(board.step) != threat_unit->cell_at(board.step - 1)
                && threat_unit->cell_at(board.step) == threat_unit->cell_at(board.step - 2)
                && threat_unit->cell_at(board.step - 1) == threat_unit->cell_at(board.step - 3)) {
                threat_unit->oscillating_unit = this;
                //if (this->_log_cond()) LUX_LOG(*threat_unit << " oscillating with " << *this);
            }
        }
    }
}

void Unit::update_is_trapped() {
    this->is_trapped = true;

    for (Cell *move_cell : this->cell()->neighbors_plus) {
        if (move_cell->opp_factory(this->player)) continue;
        if (this->move_risk(move_cell) <= 0) {
            this->is_trapped = false;
            return;
        }
    }

    LUX_LOG("TRAPPED " << *this << ' ' << *this->cell());
}

int Unit::steps_until_power(int amount) {
    amount = MIN(amount, this->cfg->BATTERY_CAPACITY);
    int power_diff = amount - this->power_init;
    if (power_diff <= 0) return 0;

    int full_days = power_diff / (30 * this->cfg->CHARGE);
    int remainder_power = power_diff % (30 * this->cfg->CHARGE);

    int _power = 0;
    int step_count = 0;
    while (_power < remainder_power) {
        _power += this->power_gain(board.step + step_count++);
    }

    return 50 * full_days + step_count;
}

void Unit::expand_raw_action_queue() {
    vector<ActionSpec> temp_aq(this->raw_action_queue, this->raw_action_queue + this->raq_len);
    int idx = 0;
    while (idx < (int)temp_aq.size()) {
        ActionSpec *spec = &temp_aq[idx++];
        if (spec->action == UnitAction_RECHARGE) {
            spec->n = MAX(spec->n, this->steps_until_power(spec->amount));
        }
        while (spec->n > 0) {
            this->action_queue[this->aq_len++] = *spec;
            spec->n -= 1;
            if (this->aq_len >= UNIT_AQ_BUF_LEN) return;
        }
        if (spec->repeat) {
            // Last item is repeating: normalize by reducing repeat to 1
            if (idx == (int)temp_aq.size() - 1) spec->repeat = 1;
            spec->n = spec->repeat;
            temp_aq.push_back(*spec);
        }
    }
}

void Unit::compress_new_action_queue() {
    vector<ActionSpec> temp_aq;
    size_t idx = 0, j;
    while (idx < this->new_action_queue.size()) {
        ActionSpec *spec = &this->new_action_queue[idx];

        // Don't give opp warning that we are picking up water
        if (spec->action == UnitAction_PICKUP && spec->resource == Resource_WATER && idx > 0) break;

        for (j = idx + 1; j <= this->new_action_queue.size(); j++) {
            if (j == this->new_action_queue.size() || !spec->equal(&this->new_action_queue[j])) break;
        }
        spec->n = j - idx;
        idx = j;

        temp_aq.push_back(*spec);
        if (temp_aq.size() == UNIT_ACTION_QUEUE_SIZE) break;
    }

    // Repeat the last action, maybe it'll make sense
    if (temp_aq.size() >= 1) {
        temp_aq[temp_aq.size() - 1].repeat = 1;
    }

    // Repeat the last two actions if there's a chance it'll make sense
    if (temp_aq.size() >= 2) {
        ActionSpec *spec1 = &temp_aq[temp_aq.size() - 1];
        ActionSpec *spec2 = &temp_aq[temp_aq.size() - 2];
        if (spec1->action == UnitAction_MOVE && spec2->action == UnitAction_MOVE
            && spec1->n == 1 && spec2->n == 1
            && ((spec1->direction == Direction_WEST && spec2->direction == Direction_EAST)
                || (spec1->direction == Direction_EAST && spec2->direction == Direction_WEST)
                || (spec1->direction == Direction_NORTH && spec2->direction == Direction_SOUTH)
                || (spec1->direction == Direction_SOUTH && spec2->direction == Direction_NORTH))) {
            spec1->repeat = 1;
            spec2->repeat = 1;
        }
        else if (spec1->action == UnitAction_TRANSFER && spec2->action == UnitAction_TRANSFER
                 && spec1->n == 1 && spec2->n == 1
                 && spec1->resource != spec2->resource
                 && spec1->direction != spec2->direction) {
            spec1->repeat = 1;
            spec2->repeat = 1;
        }
        else if (((spec1->action == UnitAction_PICKUP && spec2->action == UnitAction_TRANSFER)
                  || (spec1->action == UnitAction_TRANSFER && spec2->action == UnitAction_PICKUP))
                 && spec1->n == 1 && spec2->n == 1
                 && spec1->resource == spec2->resource) {
            spec1->repeat = 1;
            spec2->repeat = 1;
        }
    }

    // TODO: special case for chain miner: 5 digs and a transfer

    this->new_action_queue = temp_aq;
}

void Unit::normalize_action_queue() {
    int i = board.sim_step - board.step;
    if (this->player != board.player
        || this->action_queue_cost_step < board.sim_step
        || this->action_queue_cost_iou
        || i >= this->aq_len) return;

    int cost = 0;
    ActionSpec *spec = &this->action_queue[i];
    if (spec->action == UnitAction_MOVE && spec->direction != Direction_CENTER) {
        Cell *target_cell = this->cell()->neighbor(spec->direction);
        if (!target_cell || target_cell->opp_factory()) {
            // Illegal move will never be resolved (unless opp factory is destroyed)
            //LUX_LOG("Normalizing illegal move.. " << *this << ' ' << *spec);
            for (int j = i; j < UNIT_AQ_BUF_LEN; j++) {
                this->action_queue[j].action = UnitAction_MOVE;
                this->action_queue[j].direction = Direction_CENTER;
                this->action_queue[j].repeat = 1;
            }
            this->aq_len = UNIT_AQ_BUF_LEN;
            return;
        }
        cost = this->move_basic_cost(target_cell);
    } else if (spec->action == UnitAction_DIG) {
        cost = this->cfg->DIG_COST;
    } else if (spec->action == UnitAction_SELF_DESTRUCT) {
        cost = this->cfg->SELF_DESTRUCT_COST;
    }

    // Unaffordable action will not be resolved until it can be afforded
    if (cost > this->power) {
        //LUX_LOG("Normalizing unaffordable move.. " << *this << ' ' << *spec);
        if (i + 1 < UNIT_AQ_BUF_LEN) {
            memmove(&this->action_queue[i + 1],
                    &this->action_queue[i],
                    sizeof(this->action_queue[0]) * (UNIT_AQ_BUF_LEN - i - 1));
        }
        spec->action = UnitAction_MOVE;
        spec->direction = Direction_CENTER;
        this->aq_len = MIN(this->aq_len + 1, UNIT_AQ_BUF_LEN);
    }
}

void Unit::new_role(Role *new_role) {
    if (this->role) {
        if (this->_log_cond()) LUX_LOG("X! " << *this << ' ' << *this->role);
        this->delete_role();
    }
    this->role = new_role;
    if (this->_log_cond()) LUX_LOG("   " << *this << ' ' << *this->role);

    this->role->set();
    this->update_assigned_factory();
}

void Unit::delete_role() {
    LUX_ASSERT(this->role);

    if (this->_log_cond()) LUX_LOG("X  " << *this << ' ' << *this->role);
    this->role->unset();
    this->role->teardown();
    delete this->role;
    this->role = NULL;
    // Note: unit's assigned factory will persist
}

void Unit::update_goal() {
    char prev_goal_type = this->role->goal_type;
    this->role->update_goal();
    if (prev_goal_type != this->role->goal_type && this->_log_cond()) {
        LUX_LOG(".  " << *this << ' ' << *this->role);
    }
}

int Unit::power_gain(int step) {
    step = (step == -1) ? board.sim_step : step;
    return step % CYCLE_LENGTH < DAY_LENGTH ? this->cfg->CHARGE : 0;
}

int Unit::power_gain(int step, int end_step) {
    int start_cycle = step + 50 - (step % 50);
    int end_cycle = end_step - (end_step % 50);
    int power_gain = 0;
    if (start_cycle > end_cycle) {  // intra-day range
        for (int i = step; i < end_step; i++) power_gain += this->power_gain(i);
    } else {
        // How many steps from step to start_cycle are daytime? etc
        int day_count = (MAX(0, start_cycle - step - 20)
                         + MIN(30, end_step - end_cycle)
                         + (end_cycle - start_cycle) / 50 * 30);
        power_gain = day_count * this->cfg->CHARGE;
    }
    return power_gain;
}

bool Unit::need_action_queue_cost(ActionSpec *spec) {
    if (this->action_queue_cost_step < board.sim_step) return false;

    int i = this->new_action_queue.size();  // Less than (sim_step - step) for future created units
    return this->action_queue_cost_iou || i >= this->aq_len || !spec->equal(&this->action_queue[i]);
}

void Unit::set_unit_assignment(Unit *_unit) {
    if (this->assigned_unit) {
        LUX_LOG("Error: set unit->unit: " << *this << ' ' << *this->assigned_unit << ' ' << *_unit);
    }
    this->assigned_unit = _unit;
}

void Unit::unset_unit_assignment(Unit *_unit) {
    if (this->assigned_unit != _unit) {
        LUX_LOG("Error: unset unit->unit " << *this << ' ' << *this->assigned_unit << ' ' << *_unit);
    }
    this->assigned_unit = NULL;
}

void Unit::future_route(Factory *dest_factory, int max_len, vector<Cell*> *route) {
    LUX_ASSERT(dest_factory);
    LUX_ASSERT(route);

    route->clear();
    Cell *cur_cell = this->cell();
    route->push_back(cur_cell);

    ActionSpec prev_spec = { .action = UnitAction_MOVE, .direction = Direction_CENTER };
    for (int i = 0; i < this->aq_len; i++) {
        ActionSpec &spec = this->action_queue[i];
        if (spec.action == UnitAction_MOVE
            && spec.direction != Direction_CENTER) {
            if (spec.repeat == 1
                && spec.n == 1
                && spec.action == prev_spec.action
                && spec.direction == prev_spec.direction
                && spec.repeat == prev_spec.repeat
                && spec.n == prev_spec.n) {
                // Repeating endlessly, cut off here
                break;
            }
            cur_cell = cur_cell->neighbor(spec.direction);
            prev_spec = spec;
        } else {
            prev_spec = spec;
            continue;
        }

        if (!cur_cell
            || cur_cell->opp_factory(this->player)) {
            // Invalid move, cut off here
            break;
        }

        route->push_back(cur_cell);
        if (route->size() > max_len  // route length is actually 1 shorter than vector length..
            || cur_cell->factory == dest_factory) {
            // Reached dest or maxed out len, cut off here
            break;
        }
    }
}

void Unit::update_low_power() {
    this->low_power = false;
    this->low_power_threshold = -1;
    this->low_power_route.clear();

    // Sometimes we don't need to analyze for low power
    if (RoleRecharge::cast(this->role)
        || (this->role && this->role->_goal_is_factory())) {
        return;
    }

    bool is_player = (this->player == board.player);
    Cell *cur_cell = this->cell();
    Cell *goal_cell = NULL;
    RoleBlockade *role_blockade;
    if ((role_blockade = RoleBlockade::cast(this->role))) {
        // avoid calling blockade's goal_cell function
        // important for update_goal to be called before goal_cell, which caches various things
        goal_cell = (role_blockade->target_unit ? role_blockade->target_unit->cell() : cur_cell);
    } else if (this->role) {
        goal_cell = this->role->goal_cell();
    }

    Factory *factory = (this->assigned_factory
                        ? this->assigned_factory
                        : cur_cell->nearest_factory(this->player));

    int baseline_power = 0;
    int do_something_cost = 0;
    if (is_player) {
        // To be safe, give player units a small baseline power cushion
        baseline_power = 3 * this->cfg->MOVE_COST + this->cfg->ACTION_QUEUE_POWER_COST;
        // Estimate the cost of DOING SOMETHING this step and returning to factory NEXT STEP
        if (goal_cell && goal_cell != cur_cell) {
            Cell *next_cell = cur_cell->neighbor_toward(goal_cell);
            do_something_cost = this->move_basic_cost(next_cell);
            cur_cell = next_cell;
        } else if (board.sim0() && this->move_risk(cur_cell)) {
            for (Cell *neighbor : cur_cell->neighbors) {
                do_something_cost = MAX(do_something_cost, this->move_basic_cost(neighbor));
            }
        } else if (RoleMiner::cast(this->role)
                   || RoleCow::cast(this->role)
                   || RolePillager::cast(this->role)) {
            do_something_cost = this->cfg->DIG_COST;
        } else {
            // at goal, not threatened, not a digger -> 0 cost
        }
    } else if (!cur_cell->factory) {  // opp unit
        // If no plans to move immediately, assume they will update AQ at least once
        if (this->aq_len == 0
            || this->action_queue[0].action != UnitAction_MOVE
            || this->action_queue[0].direction == Direction_CENTER) {
            baseline_power = this->cfg->ACTION_QUEUE_POWER_COST;
        }
    }

    // TODO: Start with naive estimates?
    //int dist = cur_cell->man_dist_factory(factory);
    //int end_step = board.sim_step + dist + (is_player ? 0 : -1);
    //this->low_power_threshold = baseline_power + board.naive_cost(this, cur_cell, factory->cell);
    //int power_gain = this->power_gain(board.sim_step, end_step);
    //if (this->power - do_something_cost + power_gain < this->low_power_threshold) {
    // Maybe low power! Calculate exact return route to determine for sure.

    int return_cost = board.pathfind(
        this, cur_cell, factory->cell,
        NULL, NULL, NULL, &this->low_power_route);
    if (return_cost == INT_MAX) {
        this->low_power_threshold = this->cfg->BATTERY_CAPACITY;
    } else {
        this->low_power_threshold = baseline_power + return_cost;
    }
    int end_step = board.sim_step + this->low_power_route.size()-1 + (is_player ? 0 : -1);
    int power_gain = this->power_gain(board.sim_step, end_step);
    if (this->power - do_something_cost + power_gain < this->low_power_threshold) {
        this->low_power = true;
        /*if (board.sim0() && this->id == 30) {
            LUX_LOG("Low Power " << *this << ' '
                    << this->power
                    << " - " << do_something_cost
                    << " + " << power_gain
                    << " < (" << baseline_power
                    << ") " << this->low_power_threshold);
                    }*/
    }
}

bool Unit::is_stationary(int steps) {
    Cell *cur_cell = this->cell_at(board.step);
    for (int step = MAX(0, board.step - steps); step < board.step; step++) {
        if (cur_cell->get_unit_history(step) != this) return false;
    }
    return true;
}

bool Unit::is_chain() {
    int resource_count = 0;
    int power_count = 0;
    Direction resource_dir = Direction_CENTER;
    Direction power_dir = Direction_CENTER;
    for (int i = 0; i < MIN(this->aq_len, 12); i++) {
        ActionSpec &spec = this->action_queue[i];
        if (spec.action == UnitAction_MOVE && spec.direction == Direction_CENTER) {
            continue;
        } else if (spec.action == UnitAction_RECHARGE) {
            continue;
        } else if (spec.action == UnitAction_TRANSFER) {
            if (spec.resource == Resource_POWER) {
                if (power_count == 0) power_dir = spec.direction;
                else if (power_dir != spec.direction) return false;
                power_count += 1;
            } else {
                if (resource_count == 0) resource_dir = spec.direction;
                else if (resource_dir != spec.direction) return false;
                resource_count += 1;
            }
        } else {
            return false;
        }
        if (resource_count > 0 && power_count > 0 && resource_dir != power_dir) return true;
    }
    return false;
}

bool Unit::threat_units(Cell *cell, int past_steps, int max_radius,
                        bool ignore_heavies, bool ignore_lights, vector<Unit*> *threat_units) {
    LUX_ASSERT(cell);

    if (this->heavy) ignore_lights = true;
    LUX_ASSERT(!(ignore_heavies && ignore_lights));

    if (threat_units) threat_units->clear();
    Player *opp_player = (this->player == board.player) ? board.opp : board.player;

    Cell *radius_cell = cell->radius_cell(max_radius);
    while (radius_cell) {
        for (int step = board.step; step >= MAX(0, board.step - past_steps + 1); step--) {
            Unit *unit = radius_cell->get_unit_history(step, opp_player);
            if (unit
                && unit->alive()
                && ((unit->heavy && !ignore_heavies) || (!unit->heavy && !ignore_lights))) {
                if (threat_units) {
                    threat_units->push_back(unit);
                } else {
                    return true;
                }
            }
        }
        radius_cell = cell->radius_cell(max_radius, radius_cell);
    }

    if (threat_units && !threat_units->empty()) return true;
    return false;
}

int Unit::standoff_steps(struct Unit *opp_unit) {
    if (board.sim_step > board.step) return 0;
    if (!this->antagonizer_unit) return 0;  // Standoff is a subset of antagonized

    // Opp needs to plan to be stationary (or no plan)
    if (opp_unit->aq_len >= 1
        && opp_unit->action_queue[0].action == UnitAction_MOVE
        && opp_unit->action_queue[0].direction != Direction_CENTER) return 0;

    int max_steps = 0;
    Cell *opp_cell = opp_unit->cell();
    for (int jump = 1; jump <= 2; jump++) {
        // Count number of consecutive steps (or every other step) that opp_unit has threatened unit
        int prev_step = board.step - jump;
        int standoff_count = 0;
        for (int i = (int)this->threat_unit_steps.size() - 1; i >= 0; i--) {
            Unit *threat_unit = this->threat_unit_steps[i].first;
            int threat_step = this->threat_unit_steps[i].second;
            // Allow unit to oscillate but opp_unit must be stationary
            if (threat_unit == opp_unit && threat_step == prev_step) {
                // Only a standoff if they're standing
                if (opp_unit->cell_at(prev_step) != opp_cell) break;
                if (jump == 2 && opp_unit->cell_at(prev_step + 1) != opp_cell) break;
                standoff_count++;
                prev_step -= jump;
            } else if (threat_step < prev_step) {
                break;
            }
        }
        max_steps = MAX(max_steps, standoff_count);
    }

    return max_steps;
}

bool Unit::break_standoff(struct Unit *opp_unit) {
    // Never assume opp unit will break standoff against own unit
    if (opp_unit->player == board.player) return false;

    int standoff_steps = this->standoff_steps(opp_unit);
    if (standoff_steps <= 1) return false;
    double break_chance = 0;

    // endgame
    if (board.sim_step >= 980) {
        //                                      0    1    2
        static double const break_chance_d[] = {0, 0.7, 0.9};
        static int const bc_len_d = sizeof(break_chance_d) / sizeof(break_chance_d[0]);
        break_chance = break_chance_d[MIN(standoff_steps, bc_len_d - 1)];
    }
    // low power and oscillating
    else if (RoleRecharge::cast(this->role)
        && this->cell_at(board.step) != this->cell_at(board.step - 1)) {
        //                                      0  1    2    3
        static double const break_chance_a[] = {0, 0, 0.7, 0.9};
        static int const bc_len_a = sizeof(break_chance_a) / sizeof(break_chance_a[0]);
        break_chance = break_chance_a[MIN(standoff_steps, bc_len_a - 1)];
    }
    // light against heavy
    else if (this->heavy != opp_unit->heavy) {
        //                                      0  1  2    3    4    5    6    7
        static double const break_chance_b[] = {0, 0, 0, 0.1, 0.3, 0.5, 0.7, 0.9};
        static int const bc_len_b = sizeof(break_chance_b) / sizeof(break_chance_b[0]);
        break_chance = break_chance_b[MIN(standoff_steps, bc_len_b - 1)];
    }
    // protected miner moving off factory
    else if (this->cell()->factory
             && RoleMiner::cast(this->role)
             && RoleMiner::cast(this->role)->protector) {
        //                                      0  1    2    3    4    5    6
        static double const break_chance_e[] = {0, 0, 0.1, 0.3, 0.5, 0.7, 0.9};
        static int const bc_len_e = sizeof(break_chance_e) / sizeof(break_chance_e[0]);
        break_chance = break_chance_e[MIN(standoff_steps, bc_len_e - 1)];
    }
    // light against light / heavy against heavy
    else {
        //                                      0  1  2  3    4    5    6    7    8
        static double const break_chance_c[] = {0, 0, 0, 0, 0.1, 0.3, 0.5, 0.7, 0.9};
        static int const bc_len_c = sizeof(break_chance_c) / sizeof(break_chance_c[0]);
        break_chance = break_chance_c[MIN(standoff_steps, bc_len_c - 1)];
    }
    bool ret = prandom(board.sim_step + this->id, break_chance);
    if (this->_log_cond()) LUX_LOG(*this << " break standoff " << *opp_unit << ' '
                                   << standoff_steps << ' ' << break_chance << ' ' << ret);
    return ret;
}

int Unit::move_basic_cost(Cell *move_cell, int move_cost, double rubble_movement_cost) {
    move_cost = (move_cost < 0 ? this->cfg->MOVE_COST : move_cost);
    rubble_movement_cost = (rubble_movement_cost < 0
                            ? this->cfg->RUBBLE_MOVEMENT_COST
                            : rubble_movement_cost);
    return move_cost + static_cast<int>(rubble_movement_cost * move_cell->rubble);
}

int Unit::move_count(bool include_center) {
    LUX_ASSERT(this->player == board.player);
    LUX_ASSERT(!this->cell_next());

    int count = 0;
    vector<Cell*> &neighbors = include_center ? this->cell()->neighbors_plus : this->cell()->neighbors;
    for (Cell *move_cell : neighbors) {
        // Cell claimed by own unit or opp factory
        if (move_cell->unit_next || move_cell->opp_factory(this->player)) {
            continue;
        }

        // Can't trust that this unit will be able to get out of the way
        Unit *cur_unit = move_cell->own_unit(this->player);
        if (cur_unit
            && cur_unit->power < cur_unit->cfg->ACTION_QUEUE_POWER_COST + cur_unit->cfg->MOVE_COST) {
            continue;
        }

        // Check move risk, skipping risky moves
        if (this->move_risk(move_cell) > 0) continue;

        // Make sure unit can afford this move
        int cost = this->move_cost(move_cell);
        if (this->power >= cost || move_cell == this->cell()) {
            count++;
        }
    }
    return count;
}

bool Unit::move_is_safe_from_friendly_fire(Cell *move_cell) {
    LUX_ASSERT(this->player == board.player);
    LUX_ASSERT(!this->cell_next());

    // A friendly unit has already registered a move here (assume cannot be this unit)
    if (move_cell->unit_next) {
        //if (this->_log_cond()) LUX_LOG(*this << " cannot move " << *move_cell << " A "
        //                               << *move_cell->unit_next);
        return false;
    }

    // Avoiding low power friendlies is not a concern during final night
    Unit *friend_unit = move_cell->own_unit();
    if (board.final_night()
        && !board.player->is_strain(move_cell->lichen_strain)
        && (!friend_unit
            || this->heavy
            || !friend_unit->heavy)) {
        //if (this->_log_cond()) LUX_LOG(*this << " can move! (last night) " << *move_cell << " B");
        return true;
    }

    // Does friend unit at move_cell have enough power to escape collision?
    if (friend_unit && friend_unit != this && !friend_unit->cell_next()) {
        // If friend is trying to receive a transfer at current location, do not interfere
        // TODO: not sure if this is appropriate here, no collision would occur, just bad tx/rx
        //if (friend_unit->ice_delta || friend_unit->ore_delta) {
        //    return false;
        //}

        int friend_move_count = friend_unit->move_count(/*include_center*/false);
        if (friend_move_count == 0  // No moves available
            || (friend_move_count == 1  // Only 1 move available and surrounded - too tight!
                && move_cell->is_surrounded())) {
            //if (this->_log_cond()) LUX_LOG(*this << " cannot move " << *move_cell << " C "
            //                               << *friend_unit);
            return false;
        }
    }

    // Does a neighbor cell have a unit being forced out with no other move?
    for (Cell *neighbor : move_cell->neighbors) {
        friend_unit = neighbor->own_unit();
        if (friend_unit
            && friend_unit != this
            && (neighbor->unit_next || friend_unit->move_risk(neighbor) > 0)
            && !friend_unit->cell_next()
            && friend_unit->move_risk(move_cell) <= 0
            && friend_unit->power >= friend_unit->move_cost(move_cell)) {
            int friend_move_count = friend_unit->move_count(/*include_center*/false);
            if (friend_move_count == 1) {  // Only available move is move_cell!
                //if (this->_log_cond()) LUX_LOG(*this << " cannot move " << *move_cell << " D "
                //                               << *friend_unit);
                return false;
            }
        }
    }

    return true;
}

int Unit::move_risk(Cell *move_cell, vector<Unit*> *threat_units, bool all_collisions) {
    // Moving onto own factory is always safe
    if (move_cell->own_factory(this->player)) return 0;

    // TODO:
    // Disregard opp_units that are far from own_unit at board.step?
    // Disregard all opp_units after some amount of forward sim?
    // Use AQ-predicted location for opp_unit?
    if (board.sim_step > board.step + 5) return 0;

    int risk = 0;
    bool is_own_move = this->cell() != move_cell;
    for (Cell *neighbor : move_cell->neighbors_plus) {
        Unit *opp_unit = neighbor->opp_unit(this->player);
        if (!opp_unit) continue;

        // I am heavy, opp is light: no risk
        if (this->heavy && !opp_unit->heavy && !all_collisions) continue;

        // Opp lacks power to make move: no risk
        bool is_opp_move = neighbor != move_cell;
        int move_cost = is_opp_move ? opp_unit->move_basic_cost(move_cell) : 0;
        if (opp_unit->power < move_cost) continue;

        // I am light, opp is heavy: risky
        if (!this->heavy && opp_unit->heavy) {
            //if (this->_log_cond()) LUX_LOG("Unit::move_risk A");
            int new_risk = this->move_risk(move_cell, opp_unit, threat_units);
            if (!this->break_standoff(opp_unit) && !all_collisions) {
                risk += new_risk;
            }
            continue;
        }

        // I am standing where opp used to be. Standing is risky, moving is safe.
        if (neighbor == this->cell()) {
            if (!is_own_move) {
                //if (this->_log_cond()) LUX_LOG("Unit::move_risk B");
                risk += this->move_risk(move_cell, opp_unit, threat_units);
            }
            continue;
        }

        // I stand, they move: risky
        if (is_opp_move && !is_own_move) {
            //if (this->_log_cond()) LUX_LOG("Unit::move_risk C");
            risk += this->move_risk(move_cell, opp_unit, threat_units);
            continue;
        }

        // We both move: compare power to determine risk
        if (is_opp_move && is_own_move) {
            int own_power = this->power_init;
            int opp_power = opp_unit->power_init;
            // Consider AQ cost when i==0
            if (board.sim_step == board.step) {
                Direction own_direction = this->cell()->neighbor_to_direction(move_cell);
                Direction opp_direction = neighbor->neighbor_to_direction(move_cell);
                if (!(this->aq_len > 0
                      && this->action_queue[0].action == UnitAction_MOVE
                      && this->action_queue[0].direction == own_direction))
                    own_power -= this->cfg->ACTION_QUEUE_POWER_COST;
                if (!(opp_unit->aq_len > 0
                      && opp_unit->action_queue[0].action == UnitAction_MOVE
                      && opp_unit->action_queue[0].direction == opp_direction))
                    opp_power -= opp_unit->cfg->ACTION_QUEUE_POWER_COST;
                if (opp_power > own_power
                    || (opp_power == own_power
                        && (RoleWaterTransporter::cast(this->role)
                            || RoleMiner::cast(this->role)
                            || RoleProtector::cast(this->role)))
                    || all_collisions) {
                    //if (this->_log_cond()) LUX_LOG("Unit::move_risk D");
                    int new_risk = this->move_risk(move_cell, opp_unit, threat_units);
                    if (!this->break_standoff(opp_unit) && !all_collisions) {
                        risk += new_risk;
                    }
                    continue;
                }
            }
        }
    }
    return risk;
}

int Unit::move_risk(Cell *move_cell, Unit *opp_unit, vector<Unit*> *threat_units) {
    (void)move_cell;
    (void)opp_unit;
    if (threat_units) threat_units->push_back(opp_unit);
    return 1;
}

void Unit::register_move(Direction direction) {
    // Illegal moves result in no movement
    Cell *target_cell = this->cell()->neighbor(direction);
    if (direction != Direction_CENTER && target_cell && !target_cell->opp_factory()) {
        this->x_delta = direction_x(direction);
        this->y_delta = direction_y(direction);
    } else {
        this->x_delta = 0;
        this->y_delta = 0;
    }
    this->cell_next()->unit_next = this;
}

Direction Unit::move_direction(Cell *goal_cell) {
    // Check to see if cached route exists, is relevant, and is safe
    Cell *cur_cell = this->cell();
    if (this->route.size()) {
        Cell *route_dest_cell = this->route[this->route.size() - 1];
        if ((route_dest_cell == goal_cell)
            || (goal_cell->factory_center && route_dest_cell->factory == goal_cell->factory)) {
            auto route_cur_cell_it = find(this->route.begin(), this->route.end(), cur_cell);
            if (route_cur_cell_it != this->route.end()) {
                int cur_i = route_cur_cell_it - this->route.begin();
                int next_i = MIN(cur_i + 1, (int)this->route.size() - 1);
                Cell *move_cell = this->route[next_i];
                bool safe_from_friendly = this->move_is_safe_from_friendly_fire(move_cell);
                bool safe_from_opp = !((board.step == board.sim_step
                                        || cur_cell->man_dist(route_dest_cell) <= 1)
                                       && this->move_risk(move_cell) > 0);  // near or present danger
                if (safe_from_friendly && safe_from_opp) {
                    Direction move_direction = cur_cell->neighbor_to_direction(move_cell);
                    //if (this->_log_cond()) {
                    //    LUX_LOG("Unit::move_direction " << *this << " -> " << *goal_cell << ": "
                    //            << DirectionStr[move_direction] <<' '<< *move_cell << " from cache");
                    //}
                    return move_direction;
                }
            }
        } else {
            this->route.clear();
        }
    }

    RoleBlockade *role_blockade;
    if ((role_blockade = RoleBlockade::cast(this->role))
        && role_blockade->force_direction_step.first == board.step
        && role_blockade->force_direction_step.second == board.sim_step) {
        return role_blockade->force_direction;
    }

    // Consider each possible move, comparing the most logical route from each
    double best_ideal_cost = INT_MAX;
    pair<int, double> best_risk_cost = make_pair(INT_MAX, INT_MAX);
    Direction best_direction = Direction_CENTER;
    vector<Unit*> best_threat_units;
    for (Cell *move_cell : cur_cell->neighbors_plus) {
        // Skip illegal factory cells.
        if (move_cell->opp_factory()) continue;

        // Skip already-claimed cells.
        if (!this->move_is_safe_from_friendly_fire(move_cell)) {
            // Some exceptions:
            if (this->heavy
                && RolePincer::cast(this->role)
                && (!move_cell->unit || !move_cell->unit->heavy)) {
                // Allow it
            } else {
                //if (this->_log_cond()) {
                //    LUX_LOG("Unit::move_direction " << *this << " -> " << *goal_cell << ": "
                //            << *move_cell << " unsafe-FF");
                //}
                continue;
            }
        }

        vector<Unit*> threat_units;
        vector<Unit*> *threat_units_ptr = board.sim0() ? &threat_units : NULL;
        int risk = this->move_risk(move_cell, threat_units_ptr);

        // If player unit is taking cur_cell, increase risk if cannot afford this move_cell
        Direction move_direction = cur_cell->neighbor_to_direction(move_cell);
        if (cur_cell->unit_next && this->power < this->move_cost(move_direction)) risk += 10;

        // If we can safely move directly to goal, do it.
        if (move_cell == goal_cell && !goal_cell->factory_center && risk <= 0) {
            return cur_cell->neighbor_to_direction(move_cell);
        }

        double cost_mult = -1;
        int cost = INT_MAX;
        int move_cost = (cur_cell == move_cell) ? 0 : this->move_basic_cost(move_cell);
        vector<Cell*> move_route;
        RoleMiner *this_role_miner = RoleMiner::cast(this->role);

        // Very safe route:
        //   Keep distance from opp factories, avoid all non-defenders
        auto very_safe_avoid_cond = [&](Cell *c) {
            return ((c->assigned_unit
                     && c->assigned_unit != this
                     && !RoleDefender::cast(c->assigned_unit->role))  // Non-defender assignment
                    || (c->own_unit()
                        && RoleBlockade::cast(c->own_unit()->role)));  // Blockade (they don't assign)
        };
        auto very_safe_cost = [&](Cell *c, Unit *u) {
            int cost = u->move_basic_cost(c);
            if (c->away_dist == 1) cost += 20 * u->cfg->MOVE_COST;
            else if (c->away_dist == 2) cost += 14 * u->cfg->MOVE_COST;
            else if (c->away_dist == 3) cost += 10 * u->cfg->MOVE_COST;
            if (c->factory) cost += u->cfg->MOVE_COST;
            if (c->factory_center) cost += u->cfg->MOVE_COST;
            Unit *opp_unit;
            for (Cell *neighbor : c->neighbors_plus) {
                if ((opp_unit = neighbor->opp_unit())
                    && (opp_unit->heavy || (opp_unit->power > this->power))
                    && opp_unit->is_stationary(/*steps*/2)) {  // Stationary threat
                    cost += 10 * u->cfg->MOVE_COST;
                }
            }
            return cost;
        };
        if (cost == INT_MAX
            && RoleWaterTransporter::cast(this->role)
            && (this->water >= 5
                || this->ice >= 50
                || this->role->goal != this->assigned_factory)
            && !very_safe_avoid_cond(move_cell)) {
            cost_mult = 0.1;
            cost = board.pathfind(
                this, move_cell, goal_cell,
                NULL, very_safe_avoid_cond, very_safe_cost, &move_route);
        }

        // Safe route:
        //   Heavies: Avoid power transporters and heavies
        //   Lights: Avoid factory center, occupied factory, assigned cells, threats, heavy opp miners
        auto safe_route_avoid_cond = [&](Cell *c) {
            if (this->heavy) {
                // Protected miners can move on cells assigned to their protector
                if (this_role_miner
                    && this_role_miner->protector
                    && c->assigned_unit
                    && RoleProtector::cast(c->assigned_unit->role)) return false;
                return (
                    c->assigned_unit
                    && c->assigned_unit != this
                    && (c->assigned_unit->heavy
                        || RolePowerTransporter::cast(c->assigned_unit->role)));
            } else {  // light:
                if (c->factory_center  // Factory center
                    || (c->factory && c->unit)  // Occupied factory
                    || (c->assigned_unit && c->assigned_unit != this)) {  // Assignment
                    return true;
                }
                Unit *opp_unit;
                for (Cell *neighbor : c->neighbors_plus) {
                    // TODO: extend to neighbors as well? If so remove conditional continue
                    if (neighbor != c) continue;  // remove?
                    if ((opp_unit = neighbor->opp_unit())
                        && (((opp_unit->heavy || (opp_unit->power > this->power))
                             && opp_unit->is_stationary(/*steps*/5))  // Stationary threat
                            || ((neighbor->ice || neighbor->ore)
                                && opp_unit->heavy
                                && opp_unit->is_stationary(/*steps*/2)))) {  // Opp miner
                        return true;
                    }
                }
                return false;
            }
        };
        if (cost == INT_MAX
            && !(RoleBlockade::cast(this->role)
                 && this->role->goal_type == 'u')
            && !safe_route_avoid_cond(move_cell)) {
            cost_mult = 1;
            cost = board.pathfind(
                this, move_cell, goal_cell,
                NULL, safe_route_avoid_cond, NULL, &move_route);
        }

        // Unsafe route:
        //   Heavies: Avoid power transporters and heavy miners
        //   Lights: Avoid power transporters and heavies
        auto unsafe_route_avoid_cond = [&](Cell *c) {
            if (this->heavy) {
                return (
                    c->assigned_unit
                    && c->assigned_unit != this
                    && ((RolePowerTransporter::cast(c->assigned_unit->role)
                         && RolePowerTransporter::cast(c->assigned_unit->role)->target_unit != this)
                        || (c->assigned_unit->heavy
                            && RoleMiner::cast(c->assigned_unit->role))));
            } else {
                return (
                    c->assigned_unit
                    && c->assigned_unit != this
                    // TODO and avoid chain miners? They only have high priority in some cases..
                    && (RolePowerTransporter::cast(c->assigned_unit->role)
                        || c->assigned_unit->heavy));
            }
        };
        if (cost == INT_MAX && !unsafe_route_avoid_cond(move_cell)) {
            int move_cost = this->cfg->MOVE_COST;
            double rubble_cost = this->cfg->RUBBLE_MOVEMENT_COST;
            if (RoleBlockade::cast(this->role)
                && RoleBlockade::cast(this->role)->straightline) {
                rubble_cost = 0;
            }
            cost_mult = 4;
            cost = board.pathfind(
                this, move_cell, goal_cell,
                NULL, unsafe_route_avoid_cond,
                [&](Cell *c, Unit *u) { return u->move_basic_cost(c, move_cost, rubble_cost); },
                &move_route);
        }

        // Reckless route:
        //   Straight line
        if (cost == INT_MAX && true) {
            cost_mult = 10;
            cost = board.pathfind(this, move_cell, goal_cell, NULL, NULL, NULL, &move_route);
            if (cost == INT_MAX) {
                LUX_LOG("WARNING: BAD RECKLESS ROUTE " << *this << ' ' << *this->role
                        << ' ' << *cur_cell << ' ' << *move_cell << ' ' << *goal_cell);
                return Direction_CENTER;
            }
            //LUX_ASSERT(cost != INT_MAX);
        }

        // Modify move_cost for antagonizers to influence choice using get_antagonize_score
        double additional_cost = 0;
        if (RoleAntagonizer::cast(this->role)
            && cur_cell == RoleAntagonizer::cast(this->role)->target_cell) {
            additional_cost += (-0.25
                                * move_cell->get_antagonize_score(this->heavy)
                                * this->cfg->MOVE_COST);
        }

        // Modify move_cost for antagonizers/antagonized to prefer moving toward safety
        if (this->antagonizer_unit
            || (RoleAntagonizer::cast(this->role)
                && cur_cell == RoleAntagonizer::cast(this->role)->target_cell)) {
            additional_cost += (0.5
                                * move_cell->man_dist_factory(this->assigned_factory)
                                * this->cfg->MOVE_COST);
        }

        // TODO: Will choose a very expensive move even if ensuing route only slightly cheaper..
        double ideal_cost = cost_mult * cost + 0.5 * (move_cost + additional_cost);
        pair<int, double> risk_cost = make_pair(risk, ideal_cost);

        //if (this->_log_cond()) {
        //    LUX_LOG("Unit::move_direction " << *this << " -> " << *goal_cell << ": "
        //            << DirectionStr[move_direction] << ' ' << *move_cell << ' '
        //            << ideal_cost << "=" << cost_mult << "*" << cost
        //            << "+0.5*(" << move_cost << "+" << additional_cost << ")"
        //            << ", risk_cost = " << risk << ',' << ideal_cost);
        //}

        if (ideal_cost < best_ideal_cost) {
            best_ideal_cost = ideal_cost;
            best_threat_units = threat_units;
        }
        if (risk_cost < best_risk_cost
            || (risk_cost == best_risk_cost
                && !RoleBlockade::cast(this->role)
                && prandom(board.sim_step + this->id + (int)move_direction, 0.5))) {
            best_risk_cost = risk_cost;
            best_direction = move_direction;
            this->route = move_route;
        }
    }

    // Record threats affecting this step's movement
    for (Unit *threat_unit : best_threat_units) {
        this->threat_unit_steps.push_back(make_pair(threat_unit, board.step));
    }

    //if (this->_log_cond()) {
    //    LUX_LOG("Unit::move_direction " << *this << " -> " << *goal_cell << ": "
    //            << DirectionStr[best_direction] << ", threat_count=" << best_threat_units.size());
    //}

    //if (this->_log_cond() && board.step >= 144 && board.step <= 173) {
    //    for (Cell *route_cell : this->route) {
    //        LUX_LOG("  " << *route_cell);
    //    }
    //}

    return best_direction;
}

int Unit::move_cost(Direction direction) {
    int cost = -1;
    Cell *target_cell = this->cell()->neighbor(direction);

    // Illegal moves are free, just like center moves
    if (direction == Direction_CENTER || !target_cell || target_cell->opp_factory()) {
        cost = 0;
    } else {
        cost = this->move_basic_cost(target_cell);
    }

    ActionSpec spec{.action = UnitAction_MOVE, .direction = direction};
    if (this->need_action_queue_cost(&spec)) cost += this->cfg->ACTION_QUEUE_POWER_COST;

    return cost;
}

int Unit::move_cost(Cell *neighbor) {
    LUX_ASSERT(this->cell()->man_dist(neighbor) <= 1);
    Direction direction = this->cell()->neighbor_to_direction(neighbor);
    return this->move_cost(direction);
}

void Unit::do_move(Direction direction, bool no_move) {
    //if (this->_log_cond()) LUX_LOG(*this << ' ' << *this->role << " do move "
    //                               << DirectionStr[direction]);
    this->register_move(direction);
    // Skip power cost when doing a true no-move
    if (!no_move) this->power_delta -= this->move_cost(direction);
    this->action = ActionSpec{
	.action = UnitAction_MOVE,
	.direction = direction,
	.resource = (Resource)0,
	.amount = 0,
	.repeat = 0,
	.n = 1};
}

int Unit::dig_cost() {
    int cost = this->cfg->DIG_COST;
    ActionSpec spec{.action = UnitAction_DIG};
    if (this->need_action_queue_cost(&spec)) cost += this->cfg->ACTION_QUEUE_POWER_COST;
    return cost;
}

void Unit::do_dig() {
    Cell *cur_cell = this->cell();
    if (cur_cell->rubble > 0) {
        cur_cell->rubble -= MIN(this->cfg->DIG_RUBBLE_REMOVED, cur_cell->rubble);
    } else if (cur_cell->lichen > 0) {
        cur_cell->lichen -= MIN(this->cfg->DIG_LICHEN_REMOVED, cur_cell->lichen);
        if (cur_cell->lichen <= 0) cur_cell->rubble += this->cfg->DIG_RUBBLE_REMOVED;
    } else if (cur_cell->ice) {
        this->ice_delta += this->cfg->DIG_RESOURCE_GAIN;
    } else if (cur_cell->ore) {
        this->ore_delta += this->cfg->DIG_RESOURCE_GAIN;
    }

    this->register_move(Direction_CENTER);
    this->power_delta -= this->dig_cost();
    this->action = ActionSpec{
        .action = UnitAction_DIG,
        .direction = (Direction)0,
        .resource = (Resource)0,
        .amount = 0,
        .repeat = 0,
        .n = 1};
}

int Unit::self_destruct_cost() {
    int cost = this->cfg->SELF_DESTRUCT_COST;
    ActionSpec spec{.action = UnitAction_SELF_DESTRUCT};
    if (this->need_action_queue_cost(&spec)) cost += this->cfg->ACTION_QUEUE_POWER_COST;
    return cost;
}

void Unit::do_self_destruct() {
    this->register_move(Direction_CENTER);
    this->power_delta -= this->self_destruct_cost();
    this->action = ActionSpec{
        .action = UnitAction_SELF_DESTRUCT,
        .direction = (Direction)0,
        .resource = (Resource)0,
        .amount = 0,
        .repeat = 0,
        .n = 1};
}

int Unit::transfer_cost(Cell *neighbor, Resource resource, int amount) {
    int cost = 0;
    Direction direction = this->cell()->neighbor_to_direction(neighbor);
    ActionSpec spec{
        .action = UnitAction_TRANSFER,
        .direction = direction,
        .resource = resource,
        .amount = (int16_t)amount};
    if (this->need_action_queue_cost(&spec)) cost += this->cfg->ACTION_QUEUE_POWER_COST;
    return cost;
}

void Unit::do_transfer(Cell *neighbor, Resource resource, int amount, Unit *rx_unit_override) {
    Direction direction = this->cell()->neighbor_to_direction(neighbor);
    Factory *rx_factory = neighbor->factory;
    Unit *rx_unit = rx_unit_override ? rx_unit_override : neighbor->unit_next;
    LUX_ASSERT(rx_factory || rx_unit);
    if (resource == Resource_ICE) {
        int tx = MIN(amount, this->ice);
        this->ice_delta -= tx;
        if (rx_factory) rx_factory->ice_delta += tx; else rx_unit->ice_delta += tx;
    } else if (resource == Resource_ORE) {
        int tx = MIN(amount, this->ore);
        this->ore_delta -= tx;
        if (rx_factory) rx_factory->ore_delta += tx; else rx_unit->ore_delta += tx;
    } else if (resource == Resource_POWER) {
        int tx = MIN(amount, this->power);
        this->power_delta -= tx;
        if (rx_factory) rx_factory->power_delta += tx; else rx_unit->power_delta += tx;
    } else if (resource == Resource_WATER) {
        int tx = MIN(amount, this->water);
        this->water_delta -= tx;
        if (rx_factory) rx_factory->water_delta += tx; else rx_unit->water_delta += tx;
    } else if (resource == Resource_METAL) {
        int tx = MIN(amount, this->metal);
        this->metal_delta -= tx;
        if (rx_factory) rx_factory->metal_delta += tx; else rx_unit->metal_delta += tx;
    } else {
        LUX_ASSERT(false);
    }

    this->register_move(Direction_CENTER);
    this->power_delta -= this->transfer_cost(neighbor, resource, amount);
    this->action = ActionSpec{
        .action = UnitAction_TRANSFER,
        .direction = direction,
        .resource = resource,
        .amount = (int16_t)amount,
        .repeat = 0,
        .n = 1};
}

int Unit::pickup_cost(Resource resource, int amount) {
    int cost = 0;
    ActionSpec spec{
        .action = UnitAction_PICKUP,
        .resource = resource,
        .amount = (int16_t)amount};
    if (this->need_action_queue_cost(&spec)) cost += this->cfg->ACTION_QUEUE_POWER_COST;
    return cost;
}

void Unit::do_pickup(Resource resource, int amount) {
    Factory *factory = this->cell()->factory;
    if (resource == Resource_ICE) {
        int tx = MIN(amount, factory->ice + factory->ice_delta);
        this->ice_delta += tx;
        factory->ice_delta -= tx;
    } else if (resource == Resource_ORE) {
        int tx = MIN(amount, factory->ore + factory->ore_delta);
        this->ore_delta += tx;
        factory->ore_delta -= tx;
    } else if (resource == Resource_POWER) {
        int tx = MIN(amount, factory->power + factory->power_delta);
        this->power_delta += tx;
        factory->power_delta -= tx;
    } else if (resource == Resource_WATER) {
        int tx = MIN(amount, factory->water + factory->water_delta);
        this->water_delta += tx;
        factory->water_delta -= tx;
    } else if (resource == Resource_METAL) {
        int tx = MIN(amount, factory->metal + factory->metal_delta);
        this->metal_delta += tx;
        factory->metal_delta -= tx;
    } else {
        LUX_ASSERT(false);
    }

    this->register_move(Direction_CENTER);
    this->power_delta -= this->pickup_cost(resource, amount);
    this->action = ActionSpec{
        .action = UnitAction_PICKUP,
        .direction = (Direction)0,
        .resource = resource,
        .amount = (int16_t)amount,
        .repeat = 0,
        .n = 1};
}

ostream& operator<<(ostream &os, const struct Unit &u) {
    if (u.player == &board.home.player) os << (u.heavy ? "H." : "L.") << u.id;
    else                                os << (u.heavy ? "H!" : "L!") << u.id;
    return os;
}
