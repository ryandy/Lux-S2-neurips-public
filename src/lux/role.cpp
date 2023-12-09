#include "lux/role.hpp"

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/factory.hpp"
#include "lux/log.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_miner.hpp"
#include "lux/unit.hpp"
using namespace std;


Role::Role(Unit *_unit, char goal_type) {
    this->unit = _unit;
    this->goal_type = goal_type;
    this->goal = NULL;
    this->_set_step = -1;
}

bool Role::_goal_is_factory() {
    return (this->goal_type == 'f'
            || (this->goal_type == 'c'
                && static_cast<Cell*>(this->goal)->factory == this->get_factory()));
}

void Role::_displace_unit(Unit *_unit) {
    if (_unit->assigned_unit) {
        _unit->assigned_unit->delete_role();
    }
}

void Role::_displace_unit(Cell *cell) {
    if (cell->assigned_unit) {
        //LUX_LOG("displace " << *cell << ' ' << *cell->assigned_unit);
        //if (cell->assigned_unit->role)
        //    LUX_LOG("displace " << *cell << ' ' << *cell->assigned_unit << ' '
        //            << *cell->assigned_unit->role);
        cell->assigned_unit->delete_role();
    }
}

void Role::_displace_units(vector<Cell*> &cells) {
    for (Cell *cell : cells) {
        Role::_displace_unit(cell);
    }
}

bool Role::_do_move(Cell *_goal_cell, bool allow_no_move) {
    Cell *cur_cell = this->unit->cell();
    Cell *goal_cell = _goal_cell ? _goal_cell : this->goal_cell();

    bool at_destination = (cur_cell == goal_cell
                           || (goal_cell->factory_center && goal_cell->factory == cur_cell->factory));
    bool need_to_move = !at_destination || cur_cell->unit_next;
    if (need_to_move || allow_no_move) {
        //if (this->unit->_log_cond()) LUX_LOG("Role::do_move A");
        Direction direction = this->unit->move_direction(goal_cell);
        int move_cost = this->unit->move_cost(direction);
        if (this->unit->power >= move_cost) {
            //if (this->unit->_log_cond()) LUX_LOG("Role::do_move B");
            this->unit->do_move(direction);
            return true;
        }
    }
    //if (this->unit->_log_cond()) LUX_LOG("Role::do_move C");
    return false;
}

// Checks for friendly fire, but no pathfinding or opp risk checks
bool Role::_do_move_direct(Cell *_goal_cell) {
    Cell *cur_cell = this->unit->cell();
    Cell *goal_cell = _goal_cell ? _goal_cell : this->goal_cell();

    //if (this->unit->_log_cond()) LUX_LOG("Role::do_move_direct A");
    Cell *move_cell = cur_cell->neighbor_toward(goal_cell);
    if (this->unit->move_is_safe_from_friendly_fire(move_cell)) {
        Direction direction = cur_cell->neighbor_to_direction(move_cell);
        int move_cost = this->unit->move_cost(direction);
        if (this->unit->power >= move_cost) {
            //if (this->unit->_log_cond()) LUX_LOG("Role::do_move_direct B");
            this->unit->do_move(direction);
            return true;
        }
    }
    //if (this->unit->_log_cond()) LUX_LOG("Role::do_move_direct C");
    return false;
}

bool Role::_do_move_attack_trapped_unit() {
    Cell *cur_cell = this->unit->cell();
    Cell *rc = cur_cell->radius_cell(1, 2);
    while (rc) {
        Unit *opp_unit = rc->opp_unit();
        if (opp_unit
            && opp_unit->is_trapped
            && opp_unit->heavy == this->unit->heavy
            && (!RoleBlockade::cast(this)
                || opp_unit->water >= 5)) {
            if (cur_cell->man_dist(rc) == 1) {
                // If dist1: check for safety and do it
                if (this->unit->move_risk(rc) <= 0
                    && this->_do_move_direct(rc)) {
                    LUX_LOG(*this->unit << " attack trapped unit A " << *opp_unit << *rc);
                    return true;
                }
            } else {
                // If dist2: check powers for each move (AQ costs could be different)
                //           check if other own unit could make one of the moves
                //           check for safety
                // There are likely two move options. Another unit may be able to cover one of them
                vector<Cell*> unique_move_options;
                vector<Cell*> shared_move_options;
                for (Cell *neighbor : cur_cell->neighbors) {
                    if (neighbor->factory) continue;  // can't/don't need to move onto factory
                    if (neighbor->man_dist(rc) == 1) {
                        // Check if other own unit _already_ has moved here
                        // Ensure we have enough power to beat opp unit (and no other risk)
                        if (!this->unit->move_is_safe_from_friendly_fire(neighbor)
                            || this->unit->move_risk(neighbor) > 0
                            || this->unit->power < this->unit->move_cost(neighbor)) continue;

                        // Check if other own units could attack here (shared)
                        bool shared = false;
                        for (Cell *other_attacker_cell : neighbor->neighbors) {
                            Unit *other_attacker = other_attacker_cell->own_unit();
                            if (other_attacker
                                && other_attacker != this->unit
                                && other_attacker->heavy == this->unit->heavy
                                && !other_attacker->cell_next()
                                && other_attacker->move_risk(neighbor) <= 0) {
                                shared = true;
                            }
                        }

                        if (shared) shared_move_options.push_back(neighbor);
                        else unique_move_options.push_back(neighbor);
                    }
                }

                // Pseudo-randomly choose move option
                vector<Cell*> &move_options = (
                    !unique_move_options.empty() ? unique_move_options : shared_move_options);
                if (!move_options.empty()) {
                    int idx = prandom_index(board.sim_step + this->unit->id, move_options.size());
                    LUX_ASSERT(0 <= idx && idx < move_options.size());

                    if (this->_do_move_direct(move_options[idx])) {
                        LUX_LOG(*this->unit << " attack trapped unit B " << *opp_unit
                                << *move_options[idx] << ' '
                                << idx << ' '
                                << unique_move_options.size() << ' '
                                << shared_move_options.size());
                        return true;
                    }
                }
            }
        }
        rc = cur_cell->radius_cell(1, 2, rc);
    }
    return false;
}

bool Role::_do_no_move() {
    if (!this->_do_move(this->unit->cell(), /*allow_no_move*/true)) {
        unit->do_move(Direction_CENTER, /*no_move*/true);
    }
    return true;
}

bool Role::_do_transfer_resource_to_factory(Cell *tx_cell_override, Unit *tx_unit_override) {
    if (this->unit->ice + this->unit->ore + this->unit->water + this->unit->metal == 0) return false;

    Cell *tx_cell;
    if (!tx_cell_override) {
        if (!this->_goal_is_factory()) return false;
        Cell *cur_cell = this->unit->cell();
        Factory *factory = this->get_factory();
        int dist = cur_cell->man_dist_factory(factory);
        if (dist > 1) return false;
        tx_cell = cur_cell->neighbor_toward(factory->cell);
    } else {
        tx_cell = tx_cell_override;
    }

    Resource tx_resource = (Resource)0;
    int tx_amount = 0;
    if (this->unit->water) {
        tx_resource = Resource_WATER; tx_amount = this->unit->water;
        if (tx_unit_override) tx_amount = MIN(tx_amount, (tx_unit_override->cfg->CARGO_SPACE
                                                          - tx_unit_override->water
                                                          - tx_unit_override->water_delta));
    } else if (this->unit->ice && this->unit->ice >= this->unit->ore) {
        tx_resource = Resource_ICE; tx_amount = this->unit->ice;
        if (tx_unit_override) tx_amount = MIN(tx_amount, (tx_unit_override->cfg->CARGO_SPACE
                                                          - tx_unit_override->ice
                                                          - tx_unit_override->ice_delta));
    } else if (this->unit->ore) {
        tx_resource = Resource_ORE; tx_amount = this->unit->ore;
        if (tx_unit_override) tx_amount = MIN(tx_amount, (tx_unit_override->cfg->CARGO_SPACE
                                                          - tx_unit_override->ore
                                                          - tx_unit_override->ore_delta));
    } else {
        tx_resource = Resource_METAL; tx_amount = this->unit->metal;
        if (tx_unit_override) tx_amount = MIN(tx_amount, (tx_unit_override->cfg->CARGO_SPACE
                                                          - tx_unit_override->metal
                                                          - tx_unit_override->metal_delta));
    }

    if (tx_amount > 0
        && this->unit->power >= this->unit->transfer_cost(tx_cell, tx_resource, tx_amount)) {
        this->unit->do_transfer(tx_cell, tx_resource, tx_amount, tx_unit_override);
        return true;
    }

    return false;
}

bool Role::_do_power_pickup(int max_amount, bool goal_is_factory_override, Factory *alt_factory) {
    if (!goal_is_factory_override && !this->_goal_is_factory()) {
        //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup A");
        return false;
    }

    Cell *cur_cell = this->unit->cell();
    Factory *factory = cur_cell->factory;
    if (cur_cell->factory_center
        || !factory
        || (factory != this->get_factory()
            && factory != alt_factory)) {
        //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup B");
        return false;
    }

    int desired_amount = (this->unit->cfg->BATTERY_CAPACITY
                          - (this->unit->power + this->unit->power_delta)
                          - this->unit->power_gain());
    desired_amount = MIN(desired_amount, max_amount);
    int factory_power = factory->power + factory->power_delta - factory->power_reserved();
    int amount = MIN(desired_amount, factory_power);

    if (amount > 0) {
        //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup C");
        if (this->unit->power >= this->unit->pickup_cost(Resource_POWER, amount)) {
            //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup D");
            this->unit->do_pickup(Resource_POWER, amount);
            return true;
        }
    } else if (desired_amount > 0) {
        //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup E");
        this->unit->do_move(Direction_CENTER);
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("Role::do_pickup F");
    return false;
}

bool Role::_do_move_998() {
    Cell *cur_cell = this->unit->cell();

    if (this->unit->power < this->unit->cfg->RAZE_COST) {
        if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move998 A");
        return this->_do_move_999();
    } else if (this->unit->power < this->unit->cfg->MOVE_COST + this->unit->cfg->RAZE_COST) {
        if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move998 B");
        return (this->_do_dig_999() || this->_do_move_999());
    }

    Cell *best_cell = NULL;
    int best_score = INT_MIN;
    for (Cell *move_cell : cur_cell->neighbors_plus) {
        if (move_cell->lichen > 0
            && board.opp->is_strain(move_cell->lichen_strain)
            && this->unit->power >= this->unit->move_cost(move_cell) + this->unit->cfg->RAZE_COST) {
            int score = move_cell->lichen;
            if (score > best_score) {
                best_score = score;
                best_cell = move_cell;
            }
        }
    }

    if (best_cell) {
        if (best_cell == cur_cell) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move998 C " << best_score);
            return this->_do_dig_999();
        }

        if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move998 D " << best_score);
        Direction direction = cur_cell->neighbor_to_direction(best_cell);
        this->unit->do_move(direction);
        return true;
    }

    return false;
}

bool Role::_do_move_999() {
    Cell *cur_cell = this->unit->cell();

    // I can move, but cannot raze here. Try to crash!
    if (this->unit->power >= this->unit->cfg->MOVE_COST
        && (this->unit->power < this->unit->cfg->RAZE_COST
            || cur_cell->lichen <= 0
            || board.player->is_strain(cur_cell->lichen_strain))) {
        Cell *best_cell = NULL;
        int best_score = INT_MIN;
        for (Cell *move_cell : cur_cell->neighbors_plus) {
            if (this->unit->power < this->unit->move_cost(move_cell)) continue;

            int score = INT_MIN;
            if (move_cell->lichen > 0 && board.opp->is_strain(move_cell->lichen_strain)) {
                if (move_cell->unit_next
                    || (move_cell->unit && move_cell->unit->power < move_cell->unit->cfg->MOVE_COST)) {
                    score = 1 + move_cell->lichen;
                } else if (this->unit->move_risk(move_cell, NULL, /*all_collisions*/true)) {
                    score = 1 + move_cell->lichen / 3;
                } else if (move_cell->opp_unit()) {
                    score = 1 + move_cell->lichen / 10;
                } else if (move_cell->unit
                           && move_cell->unit->power < move_cell->unit->cfg->RAZE_COST
                           && !move_cell->unit->cell_next()) {
                    score = 1 + move_cell->lichen / 3;
                } else {
                    score = 0;
                }
            }
            if (score > best_score) {
                best_score = score;
                best_cell = move_cell;
            }
        }
        if (best_cell) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move999 A " << best_score);
            Direction direction = cur_cell->neighbor_to_direction(best_cell);
            this->unit->do_move(direction);
            return true;
        }
    }

    // No lichen here, just chill
    if (cur_cell->lichen <= 0) {
        this->unit->do_move(Direction_CENTER, /*no_move*/true);
        return true;
    }

    // On own lichen, find lowest risk move
    if (this->unit->power >= this->unit->cfg->MOVE_COST
        && cur_cell->lichen > 0
        && board.player->is_strain(cur_cell->lichen_strain)) {
        Cell *best_cell = NULL;
        int best_score = INT_MAX;
        for (Cell *move_cell : cur_cell->neighbors_plus) {
            if (this->unit->power < this->unit->move_cost(move_cell)) continue;

            int score = 0;
            if (move_cell->lichen <= 0
                || board.opp->is_strain(move_cell->lichen_strain)) {
                score = -100;
            } else if (move_cell->unit_next
                       || (move_cell->unit
                           && move_cell->unit->power < move_cell->unit->cfg->MOVE_COST)) {
                score = 1 + move_cell->lichen;
            } else if (this->unit->move_risk(move_cell, NULL, /*all_collisions*/true)) {
                score = 1 + move_cell->lichen / 3;
            } else if (move_cell->opp_unit()) {
                score = 1 + move_cell->lichen / 3;
            } else if (move_cell == cur_cell) {
                score = -1;
            }

            if (score < best_score) {
                best_score = score;
                best_cell = move_cell;
            }
        }
        if (best_cell) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " move999 B " << best_score);
            Direction direction = cur_cell->neighbor_to_direction(best_cell);
            this->unit->do_move(direction);
            return true;
        }
    }

    return false;
}

bool Role::_do_dig_999() {
    Cell *cur_cell = this->unit->cell();

    if (cur_cell->lichen > 0
        && board.opp->is_strain(cur_cell->lichen_strain)) {
        if (this->unit->power >= this->unit->self_destruct_cost()) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " dig999 A");
            this->unit->do_self_destruct();
            return true;
        } else if (this->unit->power >= this->unit->dig_cost()) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " dig999 B");
            this->unit->do_dig();
            return true;
        }
    }

    if (cur_cell->rubble > 0
        && cur_cell->rubble <= this->unit->cfg->DIG_RUBBLE_REMOVED) {
        bool lichen_adj = false;
        for (Cell *neighbor : cur_cell->neighbors) {
            if (neighbor->lichen >= MIN_LICHEN_TO_SPREAD
                && board.player->is_strain(neighbor->lichen_strain)) lichen_adj = true;
            if (neighbor->own_factory()) lichen_adj = true;
        }
        if (lichen_adj
            && this->unit->power >= this->unit->dig_cost()) {
            if (board.sim0()) LUX_LOG(*this->unit << ' ' << *cur_cell << " dig999 C");
            this->unit->do_dig();
            return true;
        }
    }

    return false;
}

bool Role::_do_move_to_exploding_factory() {
    Cell *cur_cell = this->unit->cell();
    Factory *factory = cur_cell->nearest_factory(board.player);

    if (!factory
        || cur_cell->man_dist_factory(factory) > 1
        || (factory->water + factory->ice / 4) != 1) return false;

    if (this->unit->heavy
        && RoleMiner::cast(this)
        && RoleMiner::cast(this)->resource_cell->ice
        && RoleMiner::cast(this)->resource_cell == cur_cell) return false;

    bool water_nearby = false;
    Unit *u;
    for (Cell *cell : factory->cells_plus) {
        if ((u = cell->own_unit())
            && (u->water >= 2
                || u->ice >= 8)) water_nearby = true;
    }
    Cell *neighbor = factory->radius_cell(2);
    while (neighbor) {
        if ((u = neighbor->own_unit())
            && (u->water >= 2
                || u->ice >= 8)) water_nearby = true;
        neighbor = factory->radius_cell(2, neighbor);
    }
    if (water_nearby) return false;

    Cell *best_cell = NULL;
    int best_score = INT_MIN;
    for (Cell *move_cell : cur_cell->neighbors_plus) {
        if (move_cell->factory
            && this->unit->power >= this->unit->move_cost(move_cell)
            && this->unit->move_is_safe_from_friendly_fire(move_cell)) {
            int score = 0;
            if (move_cell->unit
                && move_cell->unit != this->unit
                && !move_cell->unit->cell_next()) score -= 2;
            if (move_cell->factory_center
                && factory->metal >= 10) score -= 2;
            for (Cell *neighbor : move_cell->neighbors) {
                if (neighbor->unit
                    && neighbor->unit != this->unit
                    && !neighbor->unit->cell_next()) score -= 1;
            }
            //if (board.sim0()) LUX_LOG(*this->unit << " move to exploding " << *move_cell
            //                          << ' ' << score);
            if (score > best_score) {
                best_score = score;
                best_cell = move_cell;
            }
        }
    }

    if (best_cell) {
        if (board.sim0()) LUX_LOG(*this->unit << " move to exploding " << *factory);
        Direction direction = cur_cell->neighbor_to_direction(best_cell);
        this->unit->do_move(direction);
        return true;
    }

    return false;
}

bool Role::_do_pickup_resource_from_exploding_factory() {
    Factory *factory = this->unit->cell()->factory;

    if (!factory
        || factory->water != 0
        || factory->ice >= 4) return false;

    bool water_nearby = false;
    Unit *u;
    for (Cell *cell : factory->cells_plus) {
        if ((u = cell->own_unit())
            && (u->water >= 2
                || u->ice >= 8)) water_nearby = true;
    }
    Cell *neighbor = factory->radius_cell(1);
    while (neighbor) {
        if ((u = neighbor->own_unit())
            && (u->water >= 2
                || u->ice >= 8)) water_nearby = true;
        neighbor = factory->radius_cell(1, neighbor);
    }
    if (water_nearby) return false;

    if (factory->power + factory->power_delta > 0) {
        int amount = MIN(this->unit->cfg->BATTERY_CAPACITY - this->unit->power,
                         factory->power + factory->power_delta);
        if (this->unit->heavy
            && this->unit->power >= 1000
            && amount < 100
            && factory->metal + factory->metal_delta >= 10) {
            // no-op, prioritize metal
        }
        else if (amount > 0
            && this->unit->power >= this->unit->pickup_cost(Resource_POWER, amount)) {
            if (board.sim0()) LUX_LOG(*this->unit << " power pickup from exploding " << *factory);
            this->unit->do_pickup(Resource_POWER, amount);
            return true;
        }
    }
    if (factory->metal + factory->metal_delta > 0) {
        int amount = MIN(this->unit->cfg->CARGO_SPACE - this->unit->metal,
                         factory->metal + factory->metal_delta);
        if (amount > 0
            && this->unit->power >= this->unit->pickup_cost(Resource_METAL, amount)) {
            if (board.sim0()) LUX_LOG(*this->unit << " metal pickup from exploding " << *factory);
            this->unit->do_pickup(Resource_METAL, amount);
            return true;
        }
    }
    if (factory->ore + factory->ore_delta > 0) {
        int amount = MIN(this->unit->cfg->CARGO_SPACE - this->unit->ore,
                         factory->ore + factory->ore_delta);
        if (amount > 0
            && this->unit->power >= this->unit->pickup_cost(Resource_ORE, amount)) {
            if (board.sim0()) LUX_LOG(*this->unit << " ore pickup from exploding " << *factory);
            this->unit->do_pickup(Resource_ORE, amount);
            return true;
        }
    }

    return false;
}

bool Role::_do_move_win_collision() {
    // assume role check already done
    if (!board.sim0()
        || board.step >= END_PHASE
        || this->unit->power < this->unit->cfg->BATTERY_CAPACITY / 3
        || prandom(board.sim_step + this->unit->id, 0.9)) return false;  // only do this 10% of time

    Cell *cur_cell = this->unit->cell();
    for (Cell *move_cell : cur_cell->neighbors) {
        if (move_cell->factory
            || board.player->is_strain(move_cell->lichen_strain)) continue;

        for (Cell *neighbor : move_cell->neighbors) {
            Unit *opp_unit = neighbor->opp_unit();
            if (!opp_unit
                || opp_unit->heavy != this->unit->heavy
                || opp_unit->aq_len == 0
                || opp_unit->action_queue[0].action != UnitAction_MOVE
                || opp_unit->action_queue[0].direction == Direction_CENTER) continue;

            Direction direction = opp_unit->action_queue[0].direction;
            Cell *opp_move_cell = neighbor->neighbor(direction);
            if (opp_move_cell == move_cell) {
                int my_power = this->unit->power - this->unit->cfg->ACTION_QUEUE_POWER_COST;
                int opp_power = opp_unit->power;
                if (my_power > opp_power) {
                    if (this->unit->move_risk(move_cell) <= 0
                        && this->unit->move_is_safe_from_friendly_fire(move_cell)) {
                        int move_cost = this->unit->move_cost(direction);
                        if (this->unit->power >= move_cost) {
                            LUX_LOG("Role::win_collision " << *this->unit << ' ' << *opp_unit);
                            this->unit->do_move(direction);
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

void Role::set() {
    this->_set_step = 1000 * board.step + board.sim_step;
}

void Role::unset() {
    this->_set_step = -1;
}

bool Role::is_set() {
    return this->_set_step == 1000 * board.step + board.sim_step;
}
