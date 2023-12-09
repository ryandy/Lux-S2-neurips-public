#include "lux/role_antagonizer.hpp"

#include <list>
#include <map>
#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleAntagonizer::RoleAntagonizer(Unit *_unit, Factory *_factory, Cell *_target_cell,
                                 Factory *_target_factory, Unit *_chain_miner, bool skip_factory)
    : Role(_unit, 'f'), factory(_factory), target_cell(_target_cell), target_factory(_target_factory),
      chain_miner(_chain_miner)
{
    if (skip_factory) {
        this->goal_type = 'c';
        this->goal = this->target_cell;
    } else {
        this->goal = _factory;
    }

    this->_can_destroy_factory_step = -1;
}

bool RoleAntagonizer::from_mine(Role **new_role, Unit *_unit, Resource resource,
                                int max_dist, int max_count, int max_water) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);

    //if (_unit->_log_cond()) LUX_LOG("RoleAnt::from_mine A " << *_unit);
    Factory *factory = _unit->assigned_factory;
    if (max_count && factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RoleAntagonizer::cast(r); }) >= max_count) return false;

    Cell *best_cell = NULL;
    int min_dist = INT_MAX;

    int past_steps = 15;
    int future_steps = 10;

    // All cells that have recently been mined
    auto &past_cell_steps = (_unit->heavy
                             ? board.heavy_mine_cell_steps
                             : board.light_mine_cell_steps);
    for (int i = (int)past_cell_steps.size() - 1; i >= 0; i--) {
        int step = past_cell_steps[i].second;
        if (step < board.step - past_steps) break;

        Cell *cell = past_cell_steps[i].first;
        if (resource == Resource_ICE && max_water) {
            int unit_ice = 0;
            for (Unit *u : cell->nearest_away_factory->units) {
                unit_ice += u->ice;
            }
            int total_water = cell->nearest_away_factory->total_water(/*extra_ice*/unit_ice);
            if (total_water > max_water) continue;
        }

        int dist = cell->man_dist_factory(factory);
        if (dist <= max_dist
            && dist < min_dist
            && ((cell->ice && resource == Resource_ICE) || (cell->ore && resource == Resource_ORE))
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))) {
            best_cell = cell;
            min_dist = dist;
        }
    }

    // All cells that will be mined in the near future based on AQ
    auto &future_cell_steps = (_unit->heavy
                               ? board.future_heavy_mine_cell_steps
                               : board.future_light_mine_cell_steps);
    for (int i = 0; i < (int)future_cell_steps.size(); i++) {
        int step = future_cell_steps[i].second;
        if (step > board.step + future_steps) break;

        Cell *cell = future_cell_steps[i].first;
        if (resource == Resource_ICE && max_water) {
            int unit_ice = 0;
            for (Unit *u : cell->nearest_away_factory->units) {
                unit_ice += u->ice;
            }
            int total_water = cell->nearest_away_factory->total_water(/*extra_ice*/unit_ice);
            if (total_water > max_water) continue;
        }

        int dist = cell->man_dist_factory(factory);
        if (dist <= max_dist
            && dist < min_dist
            && ((cell->ice && resource == Resource_ICE) || (cell->ore && resource == Resource_ORE))
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))) {
            best_cell = cell;
            min_dist = dist;
        }
    }

    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RoleAntagonizer(_unit, factory, best_cell);
        return true;
    }

    return false;
}

bool RoleAntagonizer::from_chain(Role **new_role, Unit *_unit, int max_dist, int max_count) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);

    //if (_unit->_log_cond()) LUX_LOG("RoleAnt::from_chain A " << *_unit);
    Factory *factory = _unit->assigned_factory;
    if (max_count
        && (factory->get_similar_unit_count(
                _unit, [&](Role *r) { return (RoleAntagonizer::cast(r)
                                              && RoleAntagonizer::cast(r)->chain_miner); })
            >= max_count)) return false;

    Cell *best_cell = NULL;
    Unit *best_chain_miner = NULL;
    int min_dist = INT_MAX;
    for (auto &opp_chain : board.opp_chains) {
        Unit *chain_miner = opp_chain.first;
        vector<Cell*> *chain_route = opp_chain.second;
        for (Cell *chain_cell : *chain_route) {
            int dist = chain_cell->man_dist_factory(factory);
            if (!chain_cell->factory
                && !chain_cell->assigned_unit
                && dist <= max_dist
                && dist < min_dist) {
                min_dist = dist;
                best_cell = chain_cell;
                best_chain_miner = chain_miner;
            }
        }
    }

    if (best_cell) {
        *new_role = new RoleAntagonizer(_unit, factory, best_cell, NULL, best_chain_miner);
        return true;
    }

    return false;
}

bool RoleAntagonizer::from_factory(Role **new_role, Unit *_unit, Factory *target_factory) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    LUX_ASSERT(target_factory);

    //if (_unit->_log_cond()) LUX_LOG("RoleAnt::from_factory A " << *_unit << ' ' << *target_factory);
    Factory *factory = _unit->assigned_factory;

    Cell *target_cell = RoleAntagonizer::_get_factory_target_cell(_unit, target_factory);
    if (target_cell) {
        Role::_displace_unit(target_cell);
        *new_role = new RoleAntagonizer(_unit, factory, target_cell, target_factory);
        return true;
    }

    return false;
}

bool RoleAntagonizer::from_transition_antagonizer_with_target_factory(Role **new_role, Unit *_unit) {
    LUX_ASSERT(new_role);
    LUX_ASSERT(_unit);
    //if (_unit->_log_cond()) LUX_LOG("RoleAntagonizer::from_transition_ant_w_target_factory A");

    RoleAntagonizer *role;
    if (!board.sim0()
        || !(role = RoleAntagonizer::cast(_unit->role))
        || !role->target_factory) return false;

    Cell *new_target_cell = RoleAntagonizer::_get_factory_target_cell(
        _unit, role->target_factory, role->target_cell);

    if (new_target_cell) {
        if (new_target_cell != role->target_cell) {
            //if (board.sim0()) LUX_LOG(*_unit << " ANT transition " << *role->target_cell << ' '
            //                          << *new_target_cell);
            Role::_displace_unit(new_target_cell);
            *new_role = new RoleAntagonizer(_unit, role->factory,new_target_cell,role->target_factory);
            return true;
        }
    } else {
        //if (board.sim0()) LUX_LOG(*_unit << " ANT transition to recharge");
        *new_role = new RoleRecharge(_unit, role->factory);
        return true;
    }

    return false;
}

bool RoleAntagonizer::from_transition_destroy_factory(Role **new_role, Unit *_unit, int max_dist) {
    LUX_ASSERT(_unit->heavy);

    if (!board.sim0()
        || _unit->power < 500) return false;

    // Various roles/situations should not transition
    Factory *factory = _unit->assigned_factory;
    if (_unit->role
        && (false
            || (RoleAttacker::cast(_unit->role)
                && RoleAttacker::cast(_unit->role)->low_power_attack)
            || (RoleMiner::cast(_unit->role)
                && RoleMiner::cast(_unit->role)->resource_cell->ice
                && factory->heavy_ice_miner_count == 1)
            || RolePincer::cast(_unit->role)
            || RoleProtector::cast(_unit->role))) {
        return false;
    }

    Cell *best_cell = NULL;
    int min_dist = INT_MAX;

    int past_steps = 15;
    int future_steps = 10;

    Cell *cur_cell = _unit->cell();
    for (int i = (int)board.heavy_mine_cell_steps.size() - 1; i >= 0; i--) {
        int step = board.heavy_mine_cell_steps[i].second;
        if (step < board.step - past_steps) break;

        Cell *cell = board.heavy_mine_cell_steps[i].first;
        int dist = cell->man_dist(cur_cell);
        if (dist <= max_dist
            && dist < min_dist
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))
            && RoleAntagonizer::_can_destroy_factory(_unit, cell, NULL, NULL, /*cushion*/100)) {
            int heavy_ice_count = 0;
            Cell *adj = cell->nearest_away_factory->cell->radius_cell(1);
            while (adj) {
                if (adj == cell
                    || (adj->ice && adj->opp_unit() && adj->opp_unit()->heavy)) heavy_ice_count++;
                adj = cell->nearest_away_factory->cell->radius_cell(1, adj);
            }
            if (heavy_ice_count <= 1) {
                best_cell = cell;
                min_dist = dist;
            }
        }
    }

    // All cells that will be mined in the near future based on AQ
    for (int i = 0; i < (int)board.future_heavy_mine_cell_steps.size(); i++) {
        int step = board.future_heavy_mine_cell_steps[i].second;
        if (step > board.step + future_steps) break;

        Cell *cell = board.future_heavy_mine_cell_steps[i].first;
        int dist = cell->man_dist_factory(factory);
        if (dist <= max_dist
            && dist < min_dist
            && (!cell->assigned_unit
                || (_unit->heavy && !cell->assigned_unit->heavy))
            && RoleAntagonizer::_can_destroy_factory(_unit, cell, NULL, NULL, /*cushion*/100)) {
            int heavy_ice_count = 0;
            Cell *adj = cell->nearest_away_factory->cell->radius_cell(1);
            while (adj) {
                if (adj == cell
                    || (adj->ice && adj->opp_unit() && adj->opp_unit()->heavy)) heavy_ice_count++;
                adj = cell->nearest_away_factory->cell->radius_cell(1, adj);
            }
            if (heavy_ice_count <= 1) {
                best_cell = cell;
                min_dist = dist;
            }
        }
    }

    if (best_cell) {
        Role::_displace_unit(best_cell);
        *new_role = new RoleAntagonizer(_unit, factory, best_cell, NULL, NULL, /*skip_factory*/true);
        LUX_LOG("Killer Ant Transition " << *_unit << ' ' << **new_role);
        return true;
    }

    return false;
}

Cell *RoleAntagonizer::_get_factory_target_cell(Unit *unit, Factory *target_factory,
                                                Cell *prev_target_cell) {
    LUX_ASSERT(unit);
    LUX_ASSERT(target_factory);
    Factory *factory = unit->assigned_factory;

    // Avoid copying factory unit list
    list<Unit*> opp_units_val;
    list<Unit*> opp_units_ref = (unit->heavy ? target_factory->heavies : target_factory->lights);
    list<Unit*> *opp_units = &opp_units_ref;

    // Try to use units that have been to factory recently, otherwise use nearby units
    if (opp_units->empty()) {
        opp_units = &opp_units_val;  // switch to local empty list
        int radius = 3;
        Cell *rc = target_factory->radius_cell(radius);
        while (rc) {
            Unit *opp_unit = rc->opp_unit();
            if (opp_unit && opp_unit->heavy == unit->heavy) {
                opp_units->push_back(opp_unit);
            }
            rc = target_factory->radius_cell(radius, rc);
        }
    }

    map<Cell*, double> scores;
    for (Unit *opp_unit : *opp_units) {
        // Score future mine cells
        for (auto &mine_cell_step : opp_unit->future_mine_cell_steps) {
            Cell *mine_cell = mine_cell_step.first;
            int step = mine_cell_step.second;
            // [board.step, board.step+10)
            if (step < board.step + 10) {
                scores[mine_cell] += (mine_cell->ice ? 100 : 10);
            } else break;
        }

        // Score past mine cells
        for (int i = opp_unit->mine_cell_steps.size() - 1; i >= 0; i--) {
            Cell *mine_cell = opp_unit->mine_cell_steps[i].first;
            int step = opp_unit->mine_cell_steps[i].second;
            // [board.step-15, board.step)
            if (mine_cell->ice && step >= board.step - 15) scores[mine_cell] += 10;
            else if (mine_cell->ore && step >= board.step - 3) scores[mine_cell] += 1;
            else if (step < board.step - 15) break;
        }
    }

    // Bonus for continuity
    if (prev_target_cell
        && (prev_target_cell->ice || prev_target_cell->ore)) {
        if (unit->threat_units(prev_target_cell, /*steps*/1, /*radius*/1)) {
            scores[prev_target_cell] += (prev_target_cell->ice ? 100 : 10);
        } else if (unit->threat_units(prev_target_cell, /*steps*/1, /*radius*/2)) {
            scores[prev_target_cell] += (prev_target_cell->ice ? 0.5 : 0.1);
        }
    }

    Cell *best_cell = NULL;
    double best_score = INT_MIN;
    for (auto &cell_score : scores) {
        Cell *cell = cell_score.first;
        if (!cell->assigned_unit
            || cell->assigned_unit == unit
            || (!cell->assigned_unit->heavy && unit->heavy)) {
            int own_dist = cell->man_dist_factory(factory);
            // Want low dist, low rubble
            double score = cell_score.second - own_dist - 0.01 * cell->rubble;
            if (score > best_score) {
                best_score = score;
                best_cell = cell;
            }
        }
    }

    // if best_cell is null and there exists 1+ dist-1 ice, go to the one closest to target_factory
    if (!best_cell && board.sim_step > 1) {
        int min_dist = INT_MAX;
        for (Cell *ice_cell : factory->ice_cells) {
            if (ice_cell->man_dist_factory(factory) > 1) break;
            int dist = ice_cell->man_dist_factory(target_factory);
            if (dist < min_dist
                && (!ice_cell->assigned_unit
                    || ice_cell->assigned_unit == unit
                    || (!ice_cell->assigned_unit->heavy && unit->heavy))) {
                min_dist = dist;
                best_cell = ice_cell;
            }
        }
    }

    return best_cell;  // can be NULL
}

bool RoleAntagonizer::_can_destroy_factory(Unit *unit, Cell *target_cell, Factory *target_factory,
                                           Unit *chain_miner, int power_cushion) {
    LUX_ASSERT(board.sim0());

    // Light can destroy.. heavy (not factory, close enough)
    if (!unit->heavy
        && chain_miner
        && chain_miner->low_power
        && chain_miner->assigned_unit) {
        if (board.sim0()) LUX_LOG(*unit << " can destroy chain miner " << *chain_miner
                                  << " w/ " << *chain_miner->assigned_unit);
        return true;
    }

    Factory *factory = unit->assigned_factory;
    Factory *opp_factory = (target_factory
                            ? target_factory
                            : target_cell->nearest_away_factory);

    // Lights can destroy factories if they're antagonizing an ice chain
    if (!unit->heavy
        && chain_miner) {
        auto it = board.opp_chains.find(chain_miner);
        if (it != board.opp_chains.end()
            && it->second->back()->ice) {
            // valid for consideration
            if (it->second->front()->factory) {
                opp_factory = it->second->front()->factory;
            }
        } else {
            return false;
        }
    } else if (!unit->heavy
               || !target_cell->ice) {
        return false;
    }

    // Be patient during offensive ice conflict
    if (ModeIceConflict::cast(factory->mode)
        && ModeIceConflict::cast(factory->mode)->offensive
        && (factory->heavies.size() == 1
            || opp_factory == ModeIceConflict::cast(factory->mode)->opp_factory)) {
        return false;
    }

    // Not the closest ice to opp factory
    if (target_cell->man_dist_factory(opp_factory)
        > opp_factory->ice_cells[0]->man_dist_factory(opp_factory)) return false;

    int oscillate_cost = 120;
    for (Cell *neighbor : target_cell->neighbors) {
        if (!neighbor->opp_factory()) {
            oscillate_cost = MIN(oscillate_cost, unit->move_basic_cost(neighbor));
        }
    }
    oscillate_cost = (oscillate_cost + unit->move_basic_cost(target_cell)) / 2;

    // Check if factory would explode after game ends
    int step_count = (unit->power - power_cushion) / oscillate_cost;
    if (board.sim_step + step_count >= 1000) return false;

    int water = opp_factory->water;
    int ice = opp_factory->ice;
    for (Unit *u : opp_factory->units) {
        water += u->water;
        ice += u->ice;
    }
    water += ice / ICE_WATER_RATIO;

    // If at a distance, assume a heavy will mine during the approach
    int dist = unit->cell()->man_dist(target_cell);
    if (dist > 1) water += g_heavy_cfg.DIG_RESOURCE_GAIN * (dist - 1) / ICE_WATER_RATIO;

    // Assume a light may be mining the whole time
    water += g_light_cfg.DIG_RESOURCE_GAIN * water / ICE_WATER_RATIO;

    bool can_destroy = (step_count >= water);
    if (board.sim0() && can_destroy) {
        LUX_LOG(*unit << " can destroy " << *opp_factory << ' ' << unit->power
                << ' ' << oscillate_cost << ' ' << step_count << ' ' << water);
    }
    return can_destroy;
}

bool RoleAntagonizer::can_destroy_factory() {
    if (board.step == this->_can_destroy_factory_step) return this->_can_destroy_factory_cache;

    // If called for the first time after sim0, cannot know for sure (we have moved, but opp has not)
    if (!board.sim0()) return false;

    // Set cache
    this->_can_destroy_factory_step = board.step;
    this->_can_destroy_factory_cache = RoleAntagonizer::_can_destroy_factory(
        this->unit, this->target_cell, this->target_factory, this->chain_miner);

    return this->_can_destroy_factory_cache;
}

void RoleAntagonizer::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string cgoal = this->goal_type == 'c' ? "*" : "";
    string mod = this->target_cell->ore ? "Ore" : "Ice";
    if (this->chain_miner) mod = "Chain";
    os << mod << "Antagonizer[" << *this->factory << fgoal << " -> "
       << *this->target_cell << cgoal << "]";
}

Factory *RoleAntagonizer::get_factory() {
    return this->factory;
}

double RoleAntagonizer::power_usage() {
    // TODO can look at rubble at/adjacent to target cell
    return 1.5 * this->unit->cfg->MOVE_COST;
}

void RoleAntagonizer::set() {
    Role::set();
    this->target_cell->set_unit_assignment(this->unit);
    if (this->unit->heavy) this->factory->heavy_antagonizer_count++;
}

void RoleAntagonizer::unset() {
    if (this->is_set()) {
        this->target_cell->unset_unit_assignment(this->unit);
        if (this->unit->heavy) this->factory->heavy_antagonizer_count--;
        Role::unset();
    }
}

void RoleAntagonizer::teardown() {
}

bool RoleAntagonizer::is_valid() {
    int PAST_STEPS = 15;
    int FUTURE_STEPS = 10;

    // Try to keep can_destroy antagonizers valid even if home factory is lost
    if (board.sim0()
        && !this->factory->alive()
        && this->factory != this->unit->assigned_factory) {
        // update factory before checking can_destroy so it is representative of future calls
        this->factory = this->unit->assigned_factory;
        this->target_factory = NULL;
        if (this->can_destroy_factory()) {
            LUX_LOG("antagonizer modify " << *this->unit << " home factory " << *this);
        } else {
            return false;
        }
    }

    if (!this->factory->alive()) {
        return false;
    }

    // No new opp info after sim0, so must still be valid
    if (!board.sim0()
        || this->target_factory
        || board.step < PAST_STEPS
        || this->can_destroy_factory()) return true;

    // Cow antagonizers invalidate if rubble is cleared
    //if (!this->chain_miner
    //    && !this->target_cell->ice
    //    && !this->target_cell->ore
    //    && !target_cell->rubble) return false;

    // Lights should invalidate if target cell taken over by opp heavy
    if (!this->unit->heavy) {
        Unit *opp_unit = this->target_cell->opp_unit();
        if (opp_unit
            && opp_unit->heavy
            && this->target_cell->get_unit_history(board.step - 1) == opp_unit
            && this->target_cell->get_unit_history(board.step - 2) == opp_unit) {
            return false;
        }
    }

    if (this->chain_miner) {
        auto it = board.opp_chains.find(this->chain_miner);
        if (it == board.opp_chains.end()) return false;
        // TODO: if target cell no longer in known chain route, try to update role?
        return true;  // the following checks are irrelevant
    }

    // Still valid if an opp unit plans to dig at target_cell
    if (this->unit->heavy && this->target_cell->future_heavy_dig_step <= board.step + FUTURE_STEPS) {
        return true;
    }
    if (!this->unit->heavy && this->target_cell->future_light_dig_step <= board.step + FUTURE_STEPS) {
        return true;
    }

    // Still valid if an opp unit has recently been at target_cell
    for (int step = board.step; step > board.step - PAST_STEPS; step--) {
        Unit *opp_unit = this->target_cell->get_unit_history(step, board.opp);
        if (opp_unit && opp_unit->heavy == this->unit->heavy) return true;
    }

    return false;
}

Cell *RoleAntagonizer::goal_cell() {
    // Override goal if on factory center
    Cell *cur_cell = this->unit->cell();
    if (cur_cell == this->factory->cell) return this->target_cell;

    // Goal is target cell
    if (this->goal_type == 'c') {
        // If adjacent to target cell and safe to stand still, do so
        //   This can force opp unit(s) to wait indefinitely to approach target cell
        // If tracking a unit to own factory-adjacent resource, always go to cell
        //   This will tire opp and give us a chance to do some mining
        if (cur_cell->man_dist(this->target_cell) == 1
            && !(this->target_factory
                 && this->target_cell->man_dist_factory(this->factory) == 1)) {
            if (this->unit->move_risk(cur_cell) <= 0
                && this->unit->move_is_safe_from_friendly_fire(cur_cell)) {
                return cur_cell;
            }
        }

        return this->target_cell;
    }

    // Goal is factory
    return this->factory->cell;
}

void RoleAntagonizer::update_goal() {
    int unit_resource = MAX(this->unit->ice, this->unit->ore);

    if (this->goal_type == 'c') {  // Done with target cell goal?
        // Handled by RoleRecharge
        int resource_threshold = this->unit->cfg->CARGO_SPACE / 2;
        if (unit_resource >= resource_threshold) {
            this->goal_type = 'f';
            this->goal = this->factory;
        }
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = 0;
        int resource_threshold = this->unit->cfg->CARGO_SPACE / 5;
        Cell *cur_cell = this->unit->cell();
        bool is_ice_conflict = ModeIceConflict::cast(this->factory->mode);

        if (is_ice_conflict
            && this->unit->heavy
            && board.sim_step < 10) {
            // Force ice conflict heavy ant to pickup before heading out at start
            power_threshold = 600;
        }
        else if (is_ice_conflict
                 && this->unit->heavy
                 && cur_cell->factory == this->factory
                 && this->unit->power < 2000
                 && this->factory->power >= 500) {
            // Pick up power if ice conflict heavy ant at factory and it has some
            power_threshold = this->unit->power + 1;
        }
        else {
            int min_moves = (is_ice_conflict ? 10 : 40);
            power_threshold = (
                this->unit->cfg->ACTION_QUEUE_POWER_COST
                + board.naive_cost(this->unit, cur_cell, this->target_cell)
                + min_moves * this->unit->cfg->MOVE_COST
                + board.naive_cost(this->unit, this->target_cell, this->factory->cell));
        }

        power_threshold = MIN(power_threshold, 0.95 * this->unit->cfg->BATTERY_CAPACITY);
        if (this->unit->power >= power_threshold && unit_resource < resource_threshold) {
            this->goal_type = 'c';
            this->goal = this->target_cell;
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleAntagonizer::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleAntagonizer::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleAntagonizer::do_move B");
    return false;
}

bool RoleAntagonizer::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAntagonizer::do_dig A");
    if (!this->target_factory) return false;

    Cell *cur_cell = this->unit->cell();
    int resource_dist = cur_cell->man_dist_factory(this->factory);

    if ((!cur_cell->ice && !cur_cell->ore)
        || (resource_dist > 1 && board.sim_step % 2 != 0)) return false;

    Cell *goal_cell = this->goal_cell();
    int opp_resource_dist = cur_cell->away_dist;

    if (cur_cell == goal_cell
        && (resource_dist < opp_resource_dist
            || cur_cell->rubble == 0)
        && this->unit->power >= this->unit->dig_cost()) {
        this->unit->do_dig();
        return true;
    }

    return false;
}

bool RoleAntagonizer::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAntagonizer::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleAntagonizer::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleAntagonizer::do_pickup A");
    return this->_do_power_pickup();
}
