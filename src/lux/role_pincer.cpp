#include "lux/role_pincer.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/unit.hpp"
using namespace std;


RolePincer::RolePincer(Unit *_unit, Factory *_factory, Unit *_target_unit,
                       Unit *_partner_unit, Cell *_stage_cell,
                       Cell *_target_cell1, Cell *_target_cell2,
                       vector<Cell*> *_route)
    : Role(_unit, 'u'), factory(_factory), target_unit(_target_unit), partner_unit(_partner_unit),
      stage_cell(_stage_cell), target_cell1(_target_cell1),
      target_cell2(_target_cell2)
{
    this->goal = _target_unit;

    LUX_ASSERT(_target_unit->oscillating_unit);
    this->oscillating_unit = _target_unit->oscillating_unit;

    // attack cell is between stage_cell and target_cells
    if (_stage_cell->man_dist(_target_cell1) == 3) {
        this->attack_cell = _stage_cell->neighbor_toward(_target_cell1);
    } else if (_stage_cell->man_dist(_target_cell2) == 3) {
        this->attack_cell = _stage_cell->neighbor_toward(_target_cell2);
    } else {
        LUX_ASSERT(false);
    }

    // Supply a special cached route
    LUX_ASSERT(_route);
    this->unit->route = *_route;
}

void RolePincer::transition_units() {
    LUX_ASSERT(board.sim0());

    vector<Unit*> target_units;
    for (Unit *u : board.opp->units()) {
        if (u->heavy
            && !u->low_power
            && !u->assigned_unit
            && u->oscillating_unit) target_units.push_back(u);
    }

    for (Unit *target_unit : target_units) {
        Cell *target_cell1 = target_unit->cell();
        Cell *target_cell2 = target_unit->cell_at(board.step - 1);
        vector<Unit*> nearby_units;
        for (Unit *u : board.player->units()) {
            // Role exceptions
            if (RolePincer::cast(u->role)
                || (RoleMiner::cast(u->role)
                    && RoleMiner::cast(u->role)->resource_cell->ice
                    && u->role->get_factory()->total_water(u->ice) < 60)
                || (RoleProtector::cast(u->role)
                    && u->role->get_factory()->total_water(
                        RoleProtector::cast(u->role)->miner_unit->ice) < 60)) {
                continue;
            }

            // Ice conflict exceptions
            ModeIceConflict *mode = ModeIceConflict::cast(u->assigned_factory->mode);
            if (mode
                && target_unit->last_factory != mode->opp_factory
                && ((RoleAntagonizer::cast(u->role)
                     || RoleMiner::cast(u->role)
                     || RoleRecharge::cast(u->role)))) {
                continue;
            }

            int dist1 = u->cell()->man_dist(target_cell1);
            int dist2 = u->cell()->man_dist(target_cell2);
            if (u->heavy
                && dist1 > 1
                && dist2 > 1
                && (dist1 <= 6 || dist2 <= 6)
                && u->power - 2 * u->cfg->ACTION_QUEUE_POWER_COST > target_unit->power) {
                nearby_units.push_back(u);
            }
        }

        Unit *best_u1 = NULL, *best_u2 = NULL;
        vector<Cell*> best_route1, best_route2;
        for (Unit *u1 : nearby_units) {
            for (Unit *u2 : nearby_units) {
                if (u1->id >= u2->id) continue;

                vector<Cell*> route1, route2;
                if (RolePincer::get_routes(target_unit, u1, u2, &route1, &route2)
                    && (best_route1.empty()
                        || (MAX(route1.size(), route2.size())
                            < MAX(best_route1.size(), best_route2.size())))) {
                    //LUX_LOG("Pincer! " << *target_unit << ' ' << *u1 << ' ' << *u2 << ' '
                    //        << *target_unit->oscillating_unit);
                    best_u1 = u1;
                    best_u2 = u2;
                    best_route1 = route1;
                    best_route2 = route2;
                }
            }
        }

        if (!best_route1.empty()) {
            Role *r1 = new RolePincer(
                best_u1, best_u1->assigned_factory, target_unit, best_u2, best_route1.back(),
                target_unit->cell(), target_unit->cell_at(board.step - 1), &best_route1);
            best_u1->new_role(r1);
            Role *r2 = new RolePincer(
                best_u2, best_u2->assigned_factory, target_unit, best_u1, best_route2.back(),
                target_unit->cell(), target_unit->cell_at(board.step - 1), &best_route2);
            best_u2->new_role(r2);
        }
    }
}

bool RolePincer::get_routes(Unit *target_unit, Unit *u1, Unit *u2,
                            vector<Cell*> *out_route1, vector<Cell*> *out_route2) {
    int dx[] = {-1, -2, -2, -1, 1, 2, 2, 1};
    int dy[] = {-2, -1, 1, 2, 2, 1, -1, -2};

    Cell *best_target_cell = NULL;
    Cell *best_stage_cell1 = NULL;
    Cell *best_stage_cell2 = NULL;
    int min_dist = INT_MAX;
    int min_cost = INT_MAX;

    Cell *target_cells[2] = { target_unit->cell(), target_unit->cell_at(board.step - 1) };
    for (Cell *target_cell : target_cells) {
        if (target_cell->away_dist <= 1) continue;
        for (int i = 0; i < 8; i++) {
            Cell *stage_cell1 = target_cell->neighbor(dx[i], dy[i]);
            if (!stage_cell1 || stage_cell1->opp_factory() || stage_cell1->factory_center) continue;

            vector<Cell*> route1;
            int cost1 = board.pathfind(
                u1, u1->cell(), stage_cell1, NULL,
                [&](Cell *c) { return (c->opp_factory()
                                       || c->man_dist(target_cells[0]) <= 1
                                       || c->man_dist(target_cells[1]) <= 1); },
                NULL, &route1, /*max_dist*/3);
            if (cost1 == INT_MAX) continue;
            if (target_unit->power >= (
                    u1->power - 2 * u1->cfg->ACTION_QUEUE_POWER_COST - cost1)) continue;

            for (int _stage_cell2_dx = 1; _stage_cell2_dx <= 2; _stage_cell2_dx++) {
                int stage_cell2_dx = _stage_cell2_dx;
                int stage_cell2_dy = (stage_cell2_dx == 1 ? 2 : 1);
                if (dx[i] > 0) stage_cell2_dx *= -1;
                if (dy[i] > 0) stage_cell2_dy *= -1;
                Cell *stage_cell2 = target_cell->neighbor(stage_cell2_dx, stage_cell2_dy);
                if (!stage_cell2 || stage_cell2->opp_factory() || stage_cell2->factory_center)continue;

                vector<Cell*> route2;
                int cost2 = board.pathfind(
                    u2, u2->cell(), stage_cell2, NULL,
                    [&](Cell *c) { return (c->opp_factory()
                                           || c->man_dist(target_cells[0]) <= 1
                                           || c->man_dist(target_cells[1]) <= 1); },
                    NULL, &route2, /*max_dist*/3);
                if (cost2 == INT_MAX) continue;
                if (target_unit->power >= (
                        u2->power - 2 * u2->cfg->ACTION_QUEUE_POWER_COST - cost2)) continue;

                int dist = MAX(route1.size() - 1, route2.size() - 1);
                int cost = cost1 + cost2;

                // Add on a possible step of waiting
                if ((target_cell == target_cells[0] && dist % 2 == 0)
                    || (target_cell == target_cells[1] && dist % 2 == 1)) {
                    dist += 1;
                    route1.push_back(stage_cell1);
                    route2.push_back(stage_cell2);
                }

                if (dist < min_dist || (dist == min_dist && cost < min_cost)) {
                    min_dist = dist;
                    min_cost = cost;
                    best_target_cell = target_cell;
                    best_stage_cell1 = stage_cell1;
                    best_stage_cell2 = stage_cell2;
                    *out_route1 = route1;
                    *out_route2 = route2;
                }
            }
        }
    }

    if (best_target_cell) {
        //LUX_LOG(*best_target_cell << ' ' << *best_stage_cell1 << ' ' << *best_stage_cell2 << ' '
        //        << min_dist << ' ' << min_cost);
        return true;
    }

    return false;
}

bool RolePincer::primary() {
    return this->unit->id < this->partner_unit->id;
}

void RolePincer::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string ugoal = this->goal_type == 'u' ? "*" : "";
    os << "Pincer[" << *this->factory << fgoal << " -> "
       << *this->target_unit << *this->stage_cell << ugoal << "]";
}

Factory *RolePincer::get_factory() {
    return this->factory;
}

double RolePincer::power_usage() {
    return this->unit->cfg->ACTION_QUEUE_POWER_COST + this->unit->cfg->MOVE_COST;
}

void RolePincer::set() {
    Role::set();
    if (this->primary()) this->target_unit->set_unit_assignment(this->unit);
}

void RolePincer::unset() {
    if (this->is_set()) {
        if (this->primary()) this->target_unit->unset_unit_assignment(this->unit);
        Role::unset();
    }
}

void RolePincer::teardown() {
    if (this->partner_unit
        && RolePincer::cast(this->partner_unit->role)
        && this->partner_unit->role->is_set()) {  // prevent infinite loop delete_role/teardown/dele..
        this->partner_unit->delete_role();
    }
}

bool RolePincer::is_valid() {
    // Try to keep pincers valid even if home factory is lost
    if (board.sim0()
        && !this->factory->alive()
        && this->factory != this->unit->assigned_factory) {
        this->factory = this->unit->assigned_factory;
        LUX_LOG("pincer modify " << *this->unit << " home factory " << *this);
    }

    if (board.sim0()
        && this->primary()
        && !this->target_unit->alive()
        && this->unit->cell()->man_dist(this->target_unit->cell()) <= 1) {
        LUX_LOG("PINCER KILL " << *this->unit << ' ' << *this->partner_unit << ' '
                << *this->target_unit << ' ' << *this->target_unit->cell());
    }

    RolePincer *partner_role = RolePincer::cast(this->partner_unit->role);
    bool is_valid = (this->factory->alive()
                     && this->target_unit->alive()
                     && this->partner_unit->alive()
                     && partner_role);

    if (!board.sim0()) return is_valid;

    // Confirm target unit is still oscillating in place
    if (is_valid
        && this->target_unit->cell() != this->target_cell1
        && this->target_unit->cell() != this->target_cell2) {
        is_valid = false;
    }

    // Confirm target unit is still oscillating with unit
    if (is_valid
        && this->target_unit->oscillating_unit != this->oscillating_unit) {
        is_valid = false;
    }

    // Confirm we have not prematurely approached target unit
    Cell *cur_cell = this->unit->cell();
    if (is_valid
        && cur_cell != this->attack_cell
        && (cur_cell->man_dist(this->target_cell1) <= 1
            || cur_cell->man_dist(this->target_cell2) <= 1)) {
        is_valid = false;
    }

    // Confirm units arrive at attack cells simultaneously
    if (is_valid
        && ((cur_cell == this->attack_cell)
            != (this->partner_unit->cell() == partner_role->attack_cell))) {
        is_valid = false;
    }

    // TODO: Recalculate routes if primary?
    //       And confirm still possible/low-dist

    return is_valid;
}

Cell *RolePincer::goal_cell() {
    // Override goal if on factory center
    Cell *cur_cell = this->unit->cell();
    if (cur_cell == this->factory->cell) return this->stage_cell;

    // Goal is rubble cell
    if (this->goal_type == 'u') {
        Cell *target_cell = this->target_unit->cell();
        if (cur_cell == this->stage_cell) {
            RolePincer *partner_role = RolePincer::cast(this->partner_unit->role);
            LUX_ASSERT(partner_role);
            if (this->partner_unit->cell() == partner_role->stage_cell
                && cur_cell->man_dist(target_cell) != 3) {
                return this->attack_cell;
            } else {
                return cur_cell;
            }
        } else if (cur_cell == this->attack_cell) {
            // Note: this shouldn't matter - separate unit_group function used for attack move
            return cur_cell->neighbor_toward(target_cell);
        }
        return this->stage_cell;
    }

    // Goal is factory
    return this->factory->cell;
}

void RolePincer::update_goal() {
    if (this->goal_type == 'u') {  // Done with target_unit goal?
        // No state transition(s) supported
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        LUX_ASSERT(false);
    } else {
        LUX_ASSERT(false);
    }
}

bool RolePincer::do_move() {
    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RolePincer::do_move A");
        return true;
    }

    //if (this->unit->_log_cond()) LUX_LOG("RolePincer::do_move B");
    return this->_do_no_move();
}

bool RolePincer::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePincer::do_dig A");
    return false;
}

bool RolePincer::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePincer::do_transfer A");
    return false;
}

bool RolePincer::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RolePincer::do_pickup A");
    return false;
}
