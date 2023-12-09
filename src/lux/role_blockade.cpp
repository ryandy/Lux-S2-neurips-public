#include "lux/role_blockade.hpp"

#include <algorithm>  // find, sort
#include <set>
#include <string>

#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


RoleBlockade::RoleBlockade(Unit *_unit, Factory *_factory, Unit *_target_unit,
                           Factory *_target_factory, Unit *_partner)
    : Role(_unit, 'f'), factory(_factory), target_unit(_target_unit),
      target_factory(_target_factory), partner(_partner)
{
    LUX_ASSERT(_unit != _partner);

    this->goal = _factory;

    this->last_transporter_factory = _target_unit->last_factory;
    this->last_transporter_step = board.step;

    this->avoid_step = -1;
    this->push_step = -1;
    this->next_swap_and_idle_step = -1;
    this->straightline = false;
    this->force_direction_step = make_pair(-1, -1);

    this->_is_primary_step = -1;
    this->_is_engaged_step = -1;
    this->_goal_cell_step = -1;
    this->_target_route_step = -1;
    this->_unengaged_goal_cell_candidates_step = -1;
}

bool RoleBlockade::from_transition_block_water_transporter(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG(*_unit << " RoleBlockade::from_tr_block A");
    if (!board.sim0()
        || _unit->heavy) return false;

    // Various roles/situations should not transition
    if (_unit->role
        && (false
            || RoleBlockade::cast(_unit->role)
            || RoleWaterTransporter::cast(_unit->role))) {
        return false;
    }

    //if (_unit->_log_cond()) LUX_LOG(*_unit << " RoleBlockade::from_tr_block A2");
    Cell *cur_cell = _unit->cell();
    Factory *factory = _unit->assigned_factory;
    ModeIceConflict *mode = ModeIceConflict::cast(factory->mode);
    if (!mode
        || cur_cell->man_dist_factory(factory) >= 5) {
        //if (board.sim0()) LUX_LOG(*_unit << " RoleBlockade::from_tr_block B");
        return false;
    }

    //if (board.sim0()) LUX_LOG(*_unit << " RoleBlockade::from_tr_block C!");
    int max_count = 2;
    if (factory->get_similar_unit_count(
            _unit, [&](Role *r) { return RoleBlockade::cast(r); }) >= max_count) return false;

    //LUX_LOG("RoleBlockade::from_tr_block D");
    Factory *target_factory = mode->opp_factory;
    for (Unit *opp_unit : board.opp->units()) {
        // Must be light not currently on target factory
        if (opp_unit->heavy
            || opp_unit->cell()->factory == target_factory) continue;

        // Must have water or have plans to pick it up soon
        bool has_water = opp_unit->water >= 5;
        if (!has_water) {
            for (int i = 0; i < MIN(5, opp_unit->aq_len); i++) {
                ActionSpec &spec = opp_unit->action_queue[0];
                if (spec.action == UnitAction_PICKUP
                    && spec.resource == Resource_WATER
                    && spec.amount >= 5) {
                    has_water = true;
                }
            }
        }
        if (!has_water) continue;

        //LUX_LOG("RoleBlockade::from_tr_block E " << *opp_unit);
        Unit *partner = NULL;
        bool already_blockaded = false;
        for (Unit *u : factory->lights) {
            // Note: other blockade unit may be targeting different unit?
            if (RoleBlockade::cast(u->role)
                && RoleBlockade::cast(u->role)->target_unit == opp_unit) {
                if (RoleBlockade::cast(u->role)->partner) {
                    //if (board.sim0()) LUX_LOG(*_unit << " RoleBlockade::from_tr_block E");
                    already_blockaded = true;  // shouldn't happen?
                } else {
                    partner = u;
                }
            }
        }

        if (!already_blockaded) {
            //LUX_LOG("RoleBlockade::from_tr_block F");
            if (partner) {
                RoleBlockade::cast(partner->role)->partner = _unit;
            }
            *new_role = new RoleBlockade(_unit, factory, opp_unit, target_factory, partner);
            LUX_LOG("RoleBlockade::from_tr_block SUCCESS " << *_unit << ' ' << **new_role);
            return true;
        }
    }

    //LUX_LOG("RoleBlockade::from_tr_block H");
    return false;
}

bool RoleBlockade::from_transition_block_different_water_transporter(Role **new_role, Unit *_unit) {
    //if (_unit->_log_cond()) LUX_LOG("RoleBlockade::from_tr_block_different A");
    Factory *factory = _unit->assigned_factory;
    RoleBlockade *role;
    ModeIceConflict *mode;
    if (!board.sim0()
        || _unit->heavy
        || !(mode = ModeIceConflict::cast(factory->mode))
        || !(role = RoleBlockade::cast(_unit->role))
        || role->is_engaged()) return false;

    Factory *target_factory = mode->opp_factory;
    LUX_ASSERT(target_factory == role->target_factory);

    // Get current (perhaps only partial) route to target factory
    Cell *cur_cell = _unit->cell();
    Cell *cur_opp_cell = role->opp_cell();
    int cur_max_len = MIN(10, cur_opp_cell->man_dist_factory(target_factory));
    vector<Cell*> cur_target_route;
    if (!role->has_target_unit()) {
        cur_target_route.push_back(cur_opp_cell);
    } else {
        role->target_unit->future_route(target_factory, cur_max_len, &cur_target_route);
    }
    int cur_start_dist = cur_target_route.front()->man_dist_factory(target_factory);
    int cur_end_dist = cur_target_route.back()->man_dist_factory(target_factory);
    int cur_len = cur_target_route.size();
    int cur_progress = cur_start_dist - cur_end_dist;
    int cur_progress_rate = (cur_len == 1 ? 0 : 100 * cur_progress / (cur_len - 1));

    for (Unit *opp_unit : board.opp->units()) {
        // Must be light not currently on target factory
        if (opp_unit->heavy
            || opp_unit == role->target_unit
            || opp_unit->cell()->factory == target_factory) continue;

        // Must have water or have plans to pick it up soon
        bool has_water = opp_unit->water >= 5;
        if (!has_water) {
            for (int i = 0; i < MIN(5, opp_unit->aq_len); i++) {
                ActionSpec &spec = opp_unit->action_queue[0];
                if (spec.action == UnitAction_PICKUP
                    && spec.resource == Resource_WATER
                    && spec.amount >= 5) {
                    has_water = true;
                }
            }
        }
        if (!has_water) continue;

        Cell *opp_cell = opp_unit->cell();
        int opp_dist = cur_cell->man_dist(opp_cell);
        int max_len = MIN(10, opp_cell->man_dist_factory(target_factory));
        vector<Cell*> opp_target_route;
        opp_unit->future_route(target_factory, max_len, &opp_target_route);

        int opp_start_dist = opp_target_route.front()->man_dist_factory(target_factory);
        int opp_end_dist = opp_target_route.back()->man_dist_factory(target_factory);
        int opp_len = opp_target_route.size();
        int opp_progress = opp_start_dist - opp_end_dist;
        int opp_progress_rate = (opp_len == 1 ? 0 : 100 * opp_progress / (opp_len - 1));

        if ((opp_end_dist <= cur_end_dist - 2
             && opp_progress_rate >= cur_progress_rate)
            || (opp_progress_rate == 100
                && cur_progress_rate <= 0
                && opp_end_dist <= cur_end_dist
                && opp_dist <= 20)) {
            Unit *partner = NULL;
            bool already_blockaded = false;
            for (Unit *u : factory->lights) {
                // Note: other blockade unit may be targeting different unit?
                if (RoleBlockade::cast(u->role)
                    && RoleBlockade::cast(u->role)->target_unit == opp_unit) {
                    if (RoleBlockade::cast(u->role)->partner) {
                        already_blockaded = true;  // shouldn't happen?
                    } else {
                        partner = u;
                    }
                }
            }

            if (!already_blockaded) {
                if (partner) {
                    RoleBlockade::cast(partner->role)->partner = _unit;
                }
                *new_role = new RoleBlockade(_unit, factory, opp_unit, target_factory, partner);
                LUX_LOG("RoleBlockade::from_tr_block_diff SUCCESS " << *_unit << ' ' << **new_role);
                return true;
            }
        }
    }

    return false;
}

bool RoleBlockade::is_between(Cell *mid_cell, Cell *cell1, Cell *cell2, bool neighbors) {
    LUX_ASSERT(mid_cell);
    LUX_ASSERT(cell1);
    LUX_ASSERT(cell2);

    if (neighbors) {
        for (Cell *neighbor : mid_cell->neighbors_plus) {
            if (neighbor->is_between(cell1, cell2)) return true;
        }
    } else {
        return mid_cell->is_between(cell1, cell2);
    }
    return false;
}

bool RoleBlockade::is_between(Cell *mid_cell1, Cell *mid_cell2,
                              Cell *cell1, Cell *cell2, bool neighbors) {
    return (RoleBlockade::is_between(mid_cell1, cell1, cell2, neighbors)
            || RoleBlockade::is_between(mid_cell2, cell1, cell2, neighbors));
}

Cell *RoleBlockade::opp_cell() {
    if (this->has_target_unit()) return this->target_unit->cell();
    return this->last_transporter_factory->cell;
}

bool RoleBlockade::has_partner() {
    return (this->partner
            && this->partner->alive()
            && RoleBlockade::cast(this->partner->role)
            && RoleBlockade::cast(this->partner->role)->target_unit == this->target_unit);
}

bool RoleBlockade::has_target_unit() {
    return (this->target_unit
            && this->target_unit->alive());
}

bool RoleBlockade::is_primary() {
    if (this->_is_primary_step == board.step) {
        //LUX_LOG(*this->unit << " is_primary cache " << this->_is_primary);
        return this->_is_primary;
    }
    this->_is_primary_step = board.step;
    this->_is_primary = true;
    //LUX_LOG(*this->unit << " is_primary begin");

    if (this->has_partner()
        && this->partner->role->goal_type == 'u') {
        Cell *cur_cell = this->unit->cell();
        Cell *par_cell = this->partner->cell();
        Cell *opp_cell = this->opp_cell();
        int cur_opp_dist = cur_cell->man_dist(opp_cell);
        int par_opp_dist = par_cell->man_dist(opp_cell);
        if (cur_cell->man_dist(par_cell) == 1
            && cur_opp_dist != par_opp_dist  // should be impossible
            && (cur_opp_dist < 10 || par_opp_dist < 10)) {
            //LUX_LOG(*this->unit << " is_primary considering dist");
            this->_is_primary = (cur_opp_dist < par_opp_dist);
        } else {
            //LUX_LOG(*this->unit << " is_primary considering id ");
            this->_is_primary = (this->unit->id < this->partner->id);
        }
    }

    //LUX_LOG(*this->unit << " is_primary " << this->_is_primary);
    return this->_is_primary;
}

bool RoleBlockade::is_engaged() {
    if (this->_is_engaged_step == board.step) {
        //LUX_LOG(*this->unit << " is_engaged cache " << this->_is_engaged);
        return this->_is_engaged;
    }
    this->_is_engaged_step = board.step;
    this->_is_engaged = false;
    //LUX_LOG(*this->unit << " is_engaged begin");

    // Target unit and partner must exist
    if (!this->has_target_unit()
        || !this->has_partner()
        || this->goal_type != 'u'
        || this->partner->role->goal_type != 'u') {
        //LUX_LOG(*this->unit << " is_engaged false A");
        return false;
    }

    // Must be partner-adjacent
    Cell *cur_cell = this->unit->cell();
    Cell *par_cell = this->partner->cell();
    if (cur_cell->man_dist(par_cell) != 1) {
        //LUX_LOG(*this->unit << " is_engaged false B");
        return false;
    }

    // Must be near target unit
    Cell *opp_cell = this->opp_cell();
    int opp_dist = MIN(cur_cell->man_dist(opp_cell), par_cell->man_dist(opp_cell));
    if (opp_dist > 5) {
        //LUX_LOG(*this->unit << " is_engaged false C");
        return false;
    }

    // Must be between target unit and target factory
    if (RoleBlockade::is_between(cur_cell, par_cell,
                                 opp_cell, this->target_factory->cell,
                                 /*neighbors*/true)) {
        this->_is_engaged = true;
    }

    //LUX_LOG(*this->unit << " is_engaged " << this->_is_engaged);
    return this->_is_engaged;
}

bool RoleBlockade::is_ready_but_low_power() {
    // Target unit and partner must exist
    if (!this->has_target_unit()
        || !this->has_partner()
        || this->goal_type != 'u'
        || this->partner->role->goal_type != 'u') {
        return false;
    }

    // Must be close (not necessarily adj) to partner
    Cell *cur_cell = this->unit->cell();
    Cell *par_cell = this->partner->cell();
    if ((this->is_primary() && cur_cell->man_dist(par_cell) > 4)
        || (!this->is_primary() && cur_cell->man_dist(par_cell) > 1)) {
        return false;
    }

    // Must be distant from target
    Cell *opp_cell = this->opp_cell();
    int opp_dist = MIN(cur_cell->man_dist(opp_cell), par_cell->man_dist(opp_cell));
    if (opp_dist <= 5) {
        return false;
    }

    // Must have low power relative to target unit
    int min_power = MIN(this->unit->power_init, this->partner->power_init);
    if (min_power > this->target_unit->power_init - opp_dist + 15) {
        return false;
    }

    // Must be between target unit and target factory
    if (!RoleBlockade::is_between(cur_cell, par_cell,
                                  opp_cell, this->target_factory->cell,
                                  /*neighbors*/true)) {
        return false;
    }

    // Must have 0 adj opp units
    if (this->unit->threat_units(cur_cell, 1, 1)
        || this->partner->threat_units(par_cell, 1, 1)) {
        return false;
    }

    return true;
}

double RoleBlockade::unengaged_goal_cell_score(Cell *cell) {
    Cell *cur_cell = this->unit->cell();
    Cell *opp_cell = this->opp_cell();

    int min_power = this->unit->power_init;
    if (this->has_partner()) min_power = MIN(min_power, this->partner->power_init);
    int opp_power = 100;
    if (this->has_target_unit()) opp_power = this->target_unit->power_init;

    int self_dist = cell->man_dist(cur_cell);
    int opp_dist = cell->man_dist(opp_cell);
    bool reachable_bonus = (self_dist <= opp_dist - 2);
    bool reachable_power_bonus = (min_power - self_dist > opp_power - (opp_dist - 2));

    int own_factory_dist = cell->man_dist_factory(this->factory);
    int opp_factory_dist = cell->man_dist_factory(this->target_factory);

    double rubble = (cell->rubble / 20 * 20) / 100.0;  // round down to nearest 20 and convert to %
    double adj_rubble = 0;  // also round down and %
    for (Cell *neighbor : cell->neighbors) adj_rubble += neighbor->rubble / 20 * 20;
    adj_rubble /= (100.0 * cell->neighbors.size());

    PDD traffic_pair = cell->get_traffic_score(NULL, /*neighbors*/true);
    double traffic = traffic_pair.first + traffic_pair.second;  // max 1.0

    Factory *other_opp_factory = cell->_nearest_factory(board.opp, /*ignore*/this->target_factory);
    int other_opp_factory_dist = (other_opp_factory
                                  ? cell->man_dist_factory(other_opp_factory)
                                  : 100);

    int factory_dx = MAX(0, abs(cell->x - this->target_factory->x) - 1);
    int factory_dy = MAX(0, abs(cell->y - this->target_factory->y) - 1);
    int opp_factory_dx = MAX(0, abs(opp_cell->x - this->target_factory->x) - 1);
    int opp_factory_dy = MAX(0, abs(opp_cell->y - this->target_factory->y) - 1);
    bool in_route_bonus = (factory_dx > 0 && factory_dy > 0);  // diagonal from factory
    if (!in_route_bonus
        && (opp_factory_dx <= 1 || opp_factory_dy <= 1)) {  // opp approach is relatively head-on
        vector<Cell*> &target_route = this->target_route();
        if (find(target_route.begin(), target_route.end(), cell) != target_route.end()) {
            in_route_bonus = true;
        }
    }

    bool self_dist0_bonus = (self_dist == 0);
    int par_dist = (this->has_partner() ? cell->man_dist(this->partner->cell()) : 10);
    bool par_dist1_bonus = (par_dist <= 1);

    int own_factory_dist_near = MIN(2, own_factory_dist);  // [0,2] want big
    int own_factory_dist_far = MAX(3, own_factory_dist);  // [3,100] want small
    int opp_factory_dist_near = MIN(8, opp_factory_dist);  // [1,8] want big
    int opp_factory_dist_far = MAX(12, opp_factory_dist);  // [12,100] want small
    int other_opp_factory_dist_near = MIN(6, other_opp_factory_dist);  // [1,6] want big
    other_opp_factory_dist = MIN(25, other_opp_factory_dist);  // [1,25] want big

    PDD features[] = {
        make_pair(self_dist, -0.25),
        make_pair(self_dist0_bonus, 3),
        make_pair(par_dist1_bonus, 2),
        make_pair(own_factory_dist_near, 0.5),
        make_pair(own_factory_dist_far, -0.5),
        make_pair(opp_factory_dist_near, 5),
        make_pair(opp_factory_dist_far, -0.5),
        make_pair(other_opp_factory_dist_near, 2),
        make_pair(other_opp_factory_dist, 3),
        make_pair(rubble, -10),  // [0.0,1.0]
        make_pair(adj_rubble, -10),  // [0.0,1.0]
        make_pair(traffic, -30),  // [0.0,1.0]
        make_pair(in_route_bonus, 5),
        make_pair(reachable_bonus, 100),
        make_pair(reachable_power_bonus, 20),
    };

    double score = 0;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
        score += features[i].first * features[i].second;
    }

    // Debug print:
    bool verbose = false;
    //if (verbose || (cell->x == 36 && cell->y == 21)) {
    if (verbose) {
        LUX_LOG(*this << " blockade cell score:");
        for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
            LUX_LOG("  " << features[i].first << " * " << features[i].second
                    << " = " << features[i].first * features[i].second);
        }
        LUX_LOG("  TOTAL = " << score);
    }

    return score;
}

vector<Cell*> &RoleBlockade::target_route() {
    if (this->_target_route_step == board.step) return this->_target_route;
    this->_target_route_step = board.step;
    this->_target_route.clear();

    Cell *cur_cell = this->unit->cell();
    Cell *par_cell = (this->has_partner() ? this->partner->cell() : NULL);
    Cell *opp_cell = this->opp_cell();
    int max_len = MIN(10, opp_cell->man_dist_factory(this->target_factory));
    if (this->has_target_unit()) {
        this->target_unit->future_route(this->target_factory, max_len, &this->_target_route);
    } else {
        this->_target_route.push_back(opp_cell);
    }

    // Assume opp will adjust route when they reach standoff
    if (this->has_target_unit()
        && (cur_cell->man_dist(opp_cell) == 2
            || (par_cell && par_cell->man_dist(opp_cell) == 2))
        && ((find(this->_target_route.begin(), this->_target_route.end(), cur_cell)
             != this->_target_route.end())
            || (par_cell
                && (find(this->_target_route.begin(), this->_target_route.end(), par_cell)
                    != this->_target_route.end())))) {
        this->_target_route.clear();
        this->_target_route.push_back(opp_cell);
    }

    Cell *end_cell = this->_target_route.back();
    if (end_cell->factory != this->target_factory) {
        vector<Cell*> end_route;
        int cost = board.pathfind(this->target_unit, end_cell, this->target_factory->cell,
                                  [&](Cell *c) { return c->factory == this->target_factory; },
                                  NULL,
                                  [&](Cell *c, Unit *u) { (void)u; return 1 + 0.0375 * c->rubble; },
                                  &end_route);
        if (cost != INT_MAX) {
            this->_target_route.pop_back();  // end route starts with end cell of existing route
            this->_target_route.insert(this->_target_route.end(), end_route.begin(), end_route.end());
        } else {
            LUX_LOG("WARNING: bad blockade target routeA " << *end_cell <<' '<< *this->target_factory);
        }
    }

    // If something went wrong, just store no route
    if (!this->_target_route.empty()
        && this->_target_route.back()->factory != this->target_factory) {
        LUX_LOG("WARNING: bad blockade target routeB " << *opp_cell << ' '<< *this->target_factory);
        this->_target_route.clear();
    }

    return this->_target_route;
}

vector<Cell*> &RoleBlockade::unengaged_goal_cell_candidates() {
    if (this->_unengaged_goal_cell_candidates_step == board.step) {
        return this->_unengaged_goal_cell_candidates;
    }
    this->_unengaged_goal_cell_candidates_step = board.step;
    this->_unengaged_goal_cell_candidates.clear();

    vector<Cell*> target_route = this->target_route();
    Cell *prev_cell = (target_route.empty() ? NULL : target_route[0]);
    std::set<Cell*> candidate_set;  // need std to disambiguate

    for (Cell *cell : target_route) {
        if (cell->opp_factory()) continue;
        int prev_factory_dx = MAX(0, abs(prev_cell->x - this->target_factory->x) - 1);
        int prev_factory_dy = MAX(0, abs(prev_cell->y - this->target_factory->y) - 1);
        for (Cell *neighbor : cell->neighbors_plus) {
            if (!neighbor->assigned_unit
                && !neighbor->opp_factory()
                && neighbor->man_dist(prev_cell) == 2) {
                // and neighbor is closer in 2 dimensions
                int factory_dx = MAX(0, abs(neighbor->x - this->target_factory->x) - 1);
                int factory_dy = MAX(0, abs(neighbor->y - this->target_factory->y) - 1);
                if ((factory_dx < prev_factory_dx && factory_dy < prev_factory_dy)
                    || (factory_dx < prev_factory_dx && factory_dy + prev_factory_dy == 0)
                    || (factory_dy < prev_factory_dy && factory_dx + prev_factory_dx == 0)) {
                    candidate_set.insert(neighbor);
                }
            }
        }
        prev_cell = cell;
    }

    // If close to partner, expand candidates to include all route cells
    if (this->has_partner()) {
        Cell *cur_cell = this->unit->cell();
        Cell *par_cell = this->partner->cell();
        Cell *opp_cell = this->opp_cell();
        int oc = cur_cell->man_dist(opp_cell);
        int cp = cur_cell->man_dist(par_cell);
        if (oc/2 - 1 + cp < oc/2 + 1) {  // when dist to partner is 1, always true
            for (Cell *route_cell : target_route) {
                if (!route_cell->assigned_unit
                    && !route_cell->opp_factory()) {
                    candidate_set.insert(route_cell);
                }
            }
        }
    }

    this->_unengaged_goal_cell_candidates.insert(
        this->_unengaged_goal_cell_candidates.end(), candidate_set.begin(), candidate_set.end());
    sort(this->_unengaged_goal_cell_candidates.begin(), this->_unengaged_goal_cell_candidates.end(),
         [&](const Cell *a, const Cell *b) { return a->id < b->id; });
    return this->_unengaged_goal_cell_candidates;
}

Cell *RoleBlockade::unengaged_goal_cell() {
    Cell *best_cell = NULL;
    double best_score = INT_MIN;
    for (Cell *cell : this->unengaged_goal_cell_candidates()) {
        if (!cell->assigned_unit
            && !cell->opp_factory()) {
            double score = this->unengaged_goal_cell_score(cell);
            if (score > best_score) {
                best_score = score;
                best_cell = cell;
            }
        }
    }

    Cell *cur_cell = this->unit->cell();
    Cell *opp_cell = this->opp_cell();
    Cell *opp_nonfactory_cell = (opp_cell->factory
                                 ? opp_cell->factory->neighbor_toward(cur_cell)
                                 : opp_cell);
    Cell *goal_cell = (best_cell ? best_cell : opp_nonfactory_cell);
    if (board.sim0()) LUX_LOG(*this->unit << ' ' << *this->factory << ' ' << *opp_nonfactory_cell
                              << " BEST " << *goal_cell << ' ' << best_score);
    return goal_cell;
}

void RoleBlockade::print(ostream &os) const {
    string fgoal = this->goal_type == 'f' ? "*" : "";
    string tgoal = this->goal_type == 'u' ? "*" : "";
    string pu = this->partner ? "L."+to_string(this->partner->id) : "none";
    string tu = this->target_unit ? "L!"+to_string(this->target_unit->id) : "none";
    os << "Blockade[" << *this->factory << fgoal << " -> "
       << tu << tgoal << " w/ " << pu << "]";
}

Factory *RoleBlockade::get_factory() {
    return this->factory;
}

double RoleBlockade::power_usage() {
    return this->unit->cfg->ACTION_QUEUE_POWER_COST + this->unit->cfg->MOVE_COST;
}

void RoleBlockade::set() {
    Role::set();
}

void RoleBlockade::unset() {
    if (this->is_set()) {
        Role::unset();
    }
}

void RoleBlockade::teardown() {
    if (this->has_partner()) {
        RoleBlockade *par_role = RoleBlockade::cast(this->partner->role);
        par_role->partner = NULL;
        par_role->_is_primary_step = -1;
        par_role->_is_engaged_step = -1;
    }
}

bool RoleBlockade::is_valid() {
    if (!board.sim0()) return true;

    // Drop partner if it dies/reassigns
    if (!this->has_partner()) {
        this->partner = NULL;
    }

    // Try to keep blockades valid even if home factory is lost
    if (!this->factory->alive()
        && this->factory != this->unit->assigned_factory
        // Cannot be reassigned to a factory with a different IC target factory
        && (!ModeIceConflict::cast(this->unit->assigned_factory->mode)
            || (ModeIceConflict::cast(this->unit->assigned_factory->mode)->opp_factory
                == this->target_factory))) {
        this->factory = this->unit->assigned_factory;
        // Note: New home factory may not be in ice conflict
        //       Stick around to see if target factory can be killed anyways
        //       Invalidate when target_unit is gone or target_factory gains water
        //       If new factory enters ice conflict later, it's ok because all roles deleted then
        LUX_LOG("blockade modify " << *this->unit << " home factory " << *this);
    }

    if (!ModeIceConflict::cast(this->factory->mode)
        && !this->has_target_unit()) {
        LUX_LOG(*this->unit << "blockade invalidate non-IC no target unit " << *this);
        return false;
    }

    if (!ModeIceConflict::cast(this->factory->mode)
        && this->target_factory->total_water() >= 100) {
        LUX_LOG(*this->unit << "blockade invalidate non-IC high water " << *this);
        return false;
    }

    //LUX_LOG(*this->unit << " RoleBlockade::is_valid A");
    bool target_is_valid = (this->factory->alive()
                            && this->target_factory->alive()
                            && this->has_target_unit()
                            && this->target_unit->cell()->factory != this->target_factory);
    if (target_is_valid) {
        // Must have water or have plans to pick it up soon
        bool has_water = this->target_unit->water >= 5;
        if (!has_water) {
            for (int i = 0; i < MIN(5, this->target_unit->aq_len); i++) {
                ActionSpec &spec = this->target_unit->action_queue[0];
                if (spec.action == UnitAction_PICKUP
                    && spec.resource == Resource_WATER
                    && spec.amount >= 5) {
                    has_water = true;
                }
            }
        }
        if (!has_water) target_is_valid = false;
    }

    if (target_is_valid) {
        this->last_transporter_step = board.step;
        if (this->target_unit->water > this->target_unit->prev_prev_water
            && this->target_unit->cell()->factory) {
            this->last_transporter_factory = this->target_unit->cell()->factory;
        }
    } else if (this->last_transporter_step == board.step - 1) {
        this->target_unit = NULL;
        this->goal_type = 'f';
        this->goal = this->factory;
    }

    bool anticipation_is_valid = (this->factory->alive()
                                  && ModeIceConflict::cast(this->factory->mode)
                                  && this->target_factory->alive()
                                  && this->last_transporter_factory
                                  && this->last_transporter_factory->alive()
                                  && this->last_transporter_factory != this->target_factory
                                  && board.step < this->last_transporter_step + 150
                                  && this->target_factory->total_water() < 150);

    if (!target_is_valid && anticipation_is_valid) {
        LUX_LOG(*this->unit << " blockade transition to anticipate "
                << *this->last_transporter_factory << " -> " << *this->target_factory);
    }

    return target_is_valid || anticipation_is_valid;
}

// Want to call this when primary and engaged and that goal cell fn returns false (primary on the move)
bool RoleBlockade::set_goal_cell_primary_intercept() {
    Cell *cur_cell = this->unit->cell();
    Cell *opp_cell = this->opp_cell();

    vector<Cell*> &target_route = this->target_route();
    int opp_factory_dist = opp_cell->man_dist_factory(this->target_factory);
    int own_factory_dist = cur_cell->man_dist_factory(this->target_factory);
    int unit_dist = cur_cell->man_dist(opp_cell);

    if (target_route.empty()) {
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary other A");
    } else if (opp_factory_dist <= own_factory_dist
               && this->has_target_unit()
               && this->unit->power >= this->target_unit->power + 5
               && !cur_cell->neighbor_toward(this->target_factory->cell)->factory) {
        this->_goal_cell = cur_cell->neighbor_toward(this->target_factory->cell);
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary other B");
    } else if (unit_dist >= 2 && target_route.size() >= 4) {
        this->_goal_cell = this->unengaged_goal_cell();
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary intercept");
    } else if (target_route.size() >= 2) {
        this->_goal_cell = target_route[target_route.size() - 2];
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary other C");
    } else {
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary other D");
    }

    // If primary is aiming at adj partner's cell for any reason
    if (this->has_partner()
        && this->_goal_cell == this->partner->cell()
        && cur_cell->man_dist(this->_goal_cell) == 1) {
        this->_goal_cell = cur_cell;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary chill actually2");
    }

    return true;
}

bool RoleBlockade::set_goal_cell_secondary_rendezvous() {
    Cell *cur_cell = this->unit->cell();
    RoleBlockade *par_role = RoleBlockade::cast(this->partner->role);
    Cell *par_goal_cell = par_role->goal_cell();
    this->_goal_cell = par_goal_cell;  // default value

    int dx = par_goal_cell->x - this->target_factory->x;
    int dy = par_goal_cell->y - this->target_factory->y;

    Cell *best_cell = NULL;
    int best_score = INT_MIN;
    for (Cell *neighbor : par_goal_cell->neighbors) {
        if (!neighbor->assigned_unit
            && !neighbor->opp_factory()) {
            int score = -board.naive_cost(this->unit, cur_cell, neighbor);
            if (abs(dx) >= abs(dy) + 2) {
                score += (neighbor->x == par_goal_cell->x ? 4 : 0);
            } else if (abs(dy) >= abs(dx) + 2) {
                score += (neighbor->y == par_goal_cell->y ? 4 : 0);
            }
            if (score > best_score) {
                best_score = score;
                best_cell = neighbor;
            }
        }
    }

    if (best_cell) {
        this->_goal_cell = best_cell;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade secondary rendezvous");
    }

    return true;
}

bool RoleBlockade::set_goal_cell_primary_engaged() {
    Cell *cur_cell = this->unit->cell();
    this->_goal_cell = cur_cell;  // default value

    // Scenario #1
    // Currently threatened
    // Note: should this be highest priority?
    //       maybe best to try other maneuvers and do this last if nothing else works well
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A");
    int min_power = MIN(this->unit->power_init, this->partner->power_init);
    Cell *par_cell = this->partner->cell();
    Cell *opp_cell = this->opp_cell();
    int opp_dist = cur_cell->man_dist(opp_cell);

    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A1");
    vector<Unit*> threat_units;
    vector<Unit*> temp;
    (void)this->unit->threat_units(cur_cell, 1, 1, false, false, &temp);  // lights and heavies
    threat_units.insert(threat_units.end(), temp.begin(), temp.end());
    (void)this->partner->threat_units(par_cell, 1, 1, false, false, &temp);  // lights and heavies
    threat_units.insert(threat_units.end(), temp.begin(), temp.end());

    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A2");
    std::set<Cell*> possible_cells;
    std::set<Cell*> probable_cells;
    for (Unit *threat_unit : threat_units) {
        //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A3 " << *threat_unit);
        if (!threat_unit->heavy
            && threat_unit->power_init < min_power) continue;  // we can swap for low power lights
        Cell *threat_cell = threat_unit->cell();
        for (Cell *neighbor : threat_cell->neighbors) {
            if (!neighbor->factory || neighbor->opp_factory()) possible_cells.insert(neighbor);
            Unit *neighbor_unit = neighbor->own_unit();
            if (neighbor_unit
                && threat_unit->heavy == neighbor_unit->heavy) probable_cells.insert(neighbor);
        }
        if (threat_unit->aq_len >= 1
            && threat_unit->action_queue[0].action == UnitAction_MOVE
            && threat_unit->action_queue[0].direction != Direction_CENTER) {
            probable_cells.insert(threat_cell->neighbor(threat_unit->action_queue[0].direction));
        } else if (threat_unit->heavy) {
            probable_cells.insert(threat_cell);
        }
    }

    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A4");
    if (!possible_cells.empty()) {
        Cell *nearest_cell = NULL;
        int min_dist = INT_MAX;
        for (Cell *c : this->target_factory->cells) {
            if (c->man_dist(opp_cell) < min_dist) {
                min_dist = c->man_dist(opp_cell);
                nearest_cell = c;
            }
        }

        vector<Cell*> cutoff_cells;
        for (Cell *neighbor : nearest_cell->neighbors) {
            if (!neighbor->factory) cutoff_cells.push_back(neighbor);
        }

        Cell *best_next_cell = NULL;
        Direction best_direction = Direction_CENTER;
        double best_score = 0;
        for (int dir = (int)Direction_BEGIN; dir < (int)Direction_END; dir++) {
            //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe A5 " << dir);
            Direction direction = (Direction)dir;
            Cell *next_cell1 = cur_cell->neighbor(direction);
            Cell *next_cell2 = par_cell->neighbor(direction);
            if (next_cell1
                && next_cell2
                && !next_cell1->unit_next
                && !next_cell2->unit_next
                && !next_cell1->opp_factory()
                && !next_cell2->opp_factory()
                && RoleBlockade::is_between(next_cell1, next_cell2,
                                            opp_cell, this->target_factory->cell,
                                            /*neighbors*/true)) {
                int worst_cutoff_dist_diff = INT_MAX;
                for (Cell *cutoff_cell : cutoff_cells) {
                    int own_cdist = cutoff_cell->man_dist(next_cell1);
                    int par_cdist = cutoff_cell->man_dist(next_cell2);
                    int opp_cdist = cutoff_cell->man_dist(opp_cell);
                    int dist_diff = opp_cdist - MIN(own_cdist, par_cdist);
                    if (dist_diff < worst_cutoff_dist_diff) {
                        worst_cutoff_dist_diff = dist_diff;
                    }
                }

                int new_opp_dist = MIN(next_cell1->man_dist(opp_cell),
                                       next_cell2->man_dist(opp_cell));
                int fdist = MIN(next_cell1->man_dist_factory(this->target_factory),
                                next_cell2->man_dist_factory(this->target_factory));

                double score = 100;
                if (find(possible_cells.begin(), possible_cells.end(), next_cell1)
                    != possible_cells.end()) score -= 10;
                if (find(possible_cells.begin(), possible_cells.end(), next_cell2)
                    != possible_cells.end()) score -= 10;
                if (find(probable_cells.begin(), probable_cells.end(), next_cell1)
                    != probable_cells.end()) score -= 200;
                if (find(probable_cells.begin(), probable_cells.end(), next_cell2)
                    != probable_cells.end()) score -= 200;

                if (new_opp_dist == 1) score += 1;
                else if (new_opp_dist == 2) score += 1.5;
                else if (new_opp_dist == 3) score += 1;

                for (Cell *neighbor : opp_cell->neighbors_plus) {
                    if (next_cell1->is_between(neighbor, this->target_factory->cell)) {
                        score += (neighbor == opp_cell ? 0.5 : 0.25);
                    }
                    if (next_cell2->is_between(neighbor, this->target_factory->cell)) {
                        score += (neighbor == opp_cell ? 0.5 : 0.25);
                    }
                }

                if (worst_cutoff_dist_diff <= -1) score -= 200;
                else if (worst_cutoff_dist_diff == 0) score -= 50;
                else if (worst_cutoff_dist_diff == 1) score -= 20;

                if (fdist == 1) score -= 3;
                else if (fdist == 2) score -= 0.5;

                for (Cell *neighbor : next_cell1->neighbors) {
                    if (find(probable_cells.begin(), probable_cells.end(), neighbor)
                        != probable_cells.end()) score -= 5;
                    Unit *neighbor_unit = neighbor->opp_unit();
                    if (neighbor_unit
                        && (neighbor_unit->heavy
                            || neighbor_unit->power_init > min_power)
                        && (find(threat_units.begin(), threat_units.end(), neighbor_unit)
                            == threat_units.end())) {
                        score -= 200;
                    }
                }

                for (Cell *neighbor : next_cell2->neighbors) {
                    if (find(probable_cells.begin(), probable_cells.end(), neighbor)
                        != probable_cells.end()) score -= 5;
                    Unit *neighbor_unit = neighbor->opp_unit();
                    if (neighbor_unit
                        && (neighbor_unit->heavy
                            || neighbor_unit->power_init > min_power)
                        && (find(threat_units.begin(), threat_units.end(), neighbor_unit)
                            == threat_units.end())) {
                        score -= 200;
                    }
                }

                if (score > best_score) {
                    best_score = score;
                    best_direction = direction;
                    best_next_cell = next_cell1;
                }
            }
        }

        if (best_next_cell) {
            this->avoid_step = board.step;
            this->force_direction = best_direction;
            this->force_direction_step = make_pair(board.step, board.sim_step);
            this->_goal_cell = best_next_cell;
            LUX_LOG(*this->unit << ' ' << *this->_goal_cell <<" blockade primary avoid "<< best_score);
            return true;
        }
    }

    // TODO create vector of safe moves to use throughout?
    //      what about safe moves for secondary?

    // Scenario #2
    // Target unit is 2-4 dist diagonal from primary
    // Slide with target unit unless we are already distant from target factory in both x and y
    // . . . . F F F .
    // . . . . F F F .
    // . . . . F F F .
    // . . . . . . . .
    // . . . P S . . .
    // . . T . . . . .
    // ~~~~~~~~~~~~~~~
    // . . . . F F F .
    // . . . . F F F .
    // . . . . F F F .
    // . . . . . . . .
    // . . P S . . . .
    // . t t . . . . .
    // . . t . . . . .
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe B");
    int opp_dx = opp_cell->x - cur_cell->x;
    int opp_dy = opp_cell->y - cur_cell->y;
    int opp_factory_dx = MAX(0, abs(opp_cell->x - this->target_factory->x) - 1);
    int opp_factory_dy = MAX(0, abs(opp_cell->y - this->target_factory->y) - 1);
    int factory_dx = MAX(0, abs(cur_cell->x - this->target_factory->x) - 1);
    int factory_dy = MAX(0, abs(cur_cell->y - this->target_factory->y) - 1);
    if (abs(opp_dx) >= 1 && abs(opp_dx) <= 2 && abs(opp_dy) >= 1 && abs(opp_dy) <= 2) {
        // If going toward factory, always slide
        // If going away, stop once we hit +4/+4
        if (opp_factory_dx <= factory_dx
            || opp_factory_dy <= factory_dy
            || abs(factory_dx) <= 4
            || abs(factory_dy) <= 4) {
            bool enough_power = true;
            if ((opp_dx == 2
                 || opp_dy == 2
                 || (opp_factory_dx && opp_factory_dy))
                && min_power <= this->target_unit->power_init + 3) {
                enough_power = false;
            }

            Cell *best_cell = NULL;
            double best_score = INT_MIN;
            for (Cell *neighbor : cur_cell->neighbors) {
                if (neighbor->man_dist(opp_cell) < opp_dist
                    && !neighbor->unit_next
                    && neighbor->is_between(opp_cell, this->target_factory->cell)
                    && !neighbor->opp_factory()) {
                    int nfactory_dx = MAX(0, abs(neighbor->x - this->target_factory->x) - 1);
                    int nfactory_dy = MAX(0, abs(neighbor->y - this->target_factory->y) - 1);
                    int ncell_dx = abs(neighbor->x - this->target_factory->x);
                    int ncell_dy = abs(neighbor->y - this->target_factory->y);
                    double score = MIN(nfactory_dx, nfactory_dy) + 0.1 * MIN(ncell_dx, ncell_dy);
                    if (score > best_score) {
                        best_score = score;
                        best_cell = neighbor;
                    }
                }
            }

            if (enough_power && best_cell) {
                this->next_swap_and_idle_step = board.step;
                RoleBlockade *par_role = RoleBlockade::cast(this->partner->role);
                par_role->next_swap_and_idle_step = board.step;

                this->_goal_cell = best_cell;
                LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary slide");
                return true;
            }
        }
    }

    // Scenario #3
    // Primary is adjacent to target unit, secondary is "behind" primary
    // Pivot to become perpendicular to target unit route
    // . . . . . . . . .
    // . T P S . F F F .
    // . . . . . F F F .
    // . . . . . F F F .
    // . . . . . . . . .
    // ~~~~~~~~~~~~~~~~~
    // . t . . . . . . .
    // t . S . . F F F .
    // . t P . . F F F .
    // . . . . . F F F .
    // . . . . . . . . .
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe C");
    int par_opp_dx = opp_cell->x - par_cell->x;
    int par_opp_dy = opp_cell->y - par_cell->y;
    if (par_opp_dx == 2 * opp_dx && par_opp_dy == 2 * opp_dy) {
        for (Cell *neighbor : cur_cell->neighbors) {
            if (neighbor != par_cell
                && !neighbor->unit_next
                && !neighbor->opp_factory()
                && (neighbor->man_dist_factory(this->target_factory)
                    <= cur_cell->man_dist_factory(this->target_factory))) {
                this->_goal_cell = neighbor;
                LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary pivot");
                return true;
            }
        }
    }

    // Scenario #4
    // Primary is adjacent to target unit, who is in a better position
    // Push toward target unit
    // . . . . . . . . .
    // . . P S . F F F .
    // . . T . . F F F .
    // . . . . . F F F .
    // . . . . . . . . .
    // ~~~~~~~~~~~~~~~~~
    // . . . . . . . . .
    // . . . . . F F F .
    // . t P S . F F F .
    // . . t . . F F F .
    // . . . . . . . . .
    // TODO: I don't think this works well when opp cell is on a factory
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe D");
    Cell *opp_nonfactory_cell = (opp_cell->factory
                                 ? opp_cell->factory->neighbor_toward(cur_cell)
                                 : opp_cell);
    if (opp_dist == 1
        && opp_factory_dx <= factory_dx
        && opp_factory_dy <= factory_dy
        && !opp_nonfactory_cell->unit_next) {
        this->push_step = board.step;
        this->_goal_cell = opp_nonfactory_cell;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary push across");
        return true;
    }

    // Scenario #5
    // Some space between primary and target unit, primary is too close to target factory
    // Push toward target unit
    // . . . . . . . . .
    // . . . . S F F F .
    // . . T . P F F F .
    // . . . . . F F F .
    // . . . . . . . . .
    // ~~~~~~~~~~~~~~~~~
    // . . . . . . . . .
    // . . t S . F F F .
    // . t t P . F F F .
    // . . t . . F F F .
    // . . . . . . . . .
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe E");
    if (opp_dist == 2
        && factory_dx <= 2
        && factory_dy <= 2
        && (opp_cell->x == cur_cell->x || opp_cell->y == cur_cell->y)) {
        Cell *target_cell = cur_cell->neighbor_toward(opp_cell);
        if (min_power - this->unit->move_basic_cost(target_cell) > this->target_unit->power_init
            && !target_cell->opp_factory()
            && !target_cell->unit_next) {
            this->push_step = board.step;
            this->next_swap_and_idle_step = board.step;
            RoleBlockade *par_role = RoleBlockade::cast(this->partner->role);
            par_role->next_swap_and_idle_step = board.step;

            this->_goal_cell = target_cell;
            LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary push away");
            return true;
        }
    }

    // Scenario #6
    // Current position still works
    // Swap if threatened by lights, chill otherwise
    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe F");
    vector<Cell*> &candidates = this->unengaged_goal_cell_candidates();
    if (opp_dist == 1
        || find(candidates.begin(), candidates.end(), cur_cell) != candidates.end()
        || find(candidates.begin(), candidates.end(), par_cell) != candidates.end()
        || (opp_dist == 2
            && RoleBlockade::is_between(cur_cell, par_cell, opp_cell, this->target_factory->cell))) {
        bool threat_exists = false;
        threat_units.clear();
        temp.clear();
        (void)this->unit->threat_units(cur_cell, 1, 1, true, false, &temp);  // lights only
        threat_units.insert(threat_units.end(), temp.begin(), temp.end());
        (void)this->partner->threat_units(par_cell, 1, 1, true, false, &temp);  // lights only
        threat_units.insert(threat_units.end(), temp.begin(), temp.end());
        for (Unit *threat_unit : threat_units) {
            if ((threat_unit->cell()->man_dist(cur_cell) == 1
                 && threat_unit->power_init >= threat_unit->move_basic_cost(cur_cell))
                || (threat_unit->cell()->man_dist(par_cell) == 1
                    && threat_unit->power_init >= threat_unit->move_basic_cost(par_cell))) {
                threat_exists = true;
            }
        }
        if (threat_exists) {
            this->_goal_cell = par_cell;
            LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary swap");
            return true;
        } else {
            this->_goal_cell = cur_cell;
            LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary chill along route");
            return true;
        }
    }

    //if (this->unit->_log_cond()) LUX_LOG("RB::sgcpe G");
    return false;
}

bool RoleBlockade::set_goal_cell_secondary_engaged() {
    Cell *cur_cell = this->unit->cell();
    Cell *par_cell = this->partner->cell();
    Cell *opp_cell = this->opp_cell();

    RoleBlockade *par_role = RoleBlockade::cast(this->partner->role);
    LUX_ASSERT(par_role);
    Cell *par_goal_cell = par_role->goal_cell();

    // if no move
    // no move
    if (!this->partner->cell_next()) {
        this->_goal_cell = cur_cell;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade secondary no move");
        return true;
    }

    // if avoid
    // move in same direction as primary
    if (par_role->avoid_step == board.step) {
        Direction direction = par_cell->neighbor_to_direction(par_goal_cell);
        this->force_direction = direction;
        this->force_direction_step = make_pair(board.step, board.sim_step);
        this->_goal_cell = cur_cell->neighbor(direction);
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade secondary avoid "
                << DirectionStr[direction]);
        return true;
    }

    // if push
    // move in direction of opp from primary
    if (par_role->push_step == board.step) {
        int opp_dx = opp_cell->x - par_cell->x;
        int opp_dy = opp_cell->y - par_cell->y;
        opp_dx = (opp_dx ? (opp_dx / abs(opp_dx)) : opp_dx);
        opp_dy = (opp_dy ? (opp_dy / abs(opp_dy)) : opp_dy);
        this->_goal_cell = cur_cell->neighbor(opp_dx, opp_dy);
        if (this->_goal_cell->opp_factory()) {
            this->_goal_cell = par_cell;
        }
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade secondary push");
        return true;
    }

    // else
    // take primary's position
    this->_goal_cell = par_cell;
    LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade secondary follow");
    return true;
}

bool RoleBlockade::set_goal_cell_chill() {
    this->_goal_cell = this->unit->cell();
    LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade chill low power");
    return true;
}

Cell *RoleBlockade::goal_cell() {
    //if (this->unit->_log_cond()) LUX_LOG(*this->unit << " RoleBlockade::goal_cell A");

    // Override goal if on factory center
    Cell *cur_cell = this->unit->cell();
    Cell *opp_cell = this->opp_cell();
    Cell *opp_nonfactory_cell = (opp_cell->factory
                                 ? opp_cell->factory->neighbor_toward(cur_cell)
                                 : opp_cell);
    if (cur_cell == this->factory->cell) {
        //LUX_LOG(*this->unit << " RoleBlockade::goal_cell A1");
        return opp_nonfactory_cell;
    }

    // Goal is factory
    if (this->goal_type == 'f') {
        //LUX_LOG(*this->unit << " RoleBlockade::goal_cell A2");
        return this->factory->cell;
    }

    // Goal is target unit, check cache first
    if (this->_goal_cell_step == board.step) {
        // We already determined our goal cell, check to see if we should swap on the following step
        if (this->has_partner()
            && this->next_swap_and_idle_step == board.step
            && cur_cell->man_dist(this->partner->cell()) == 1) {
            if (board.sim_step == board.step + 1) {
                return this->partner->cell();
            } else {
                return cur_cell;
            }
        }

        //LUX_LOG(*this->unit << " blockade use cached goal cell " << *this->_goal_cell);
        return this->_goal_cell;
    } else {
        //LUX_LOG(*this->unit << " blockade cache miss "<< this->_goal_cell_step << ' ' << board.step);
    }
    this->_goal_cell_step = board.step;
    this->_goal_cell = opp_nonfactory_cell;  // default value
    this->straightline = false;

    bool goal_ready = false;
    bool primary_engaged_and_moving = false;
    if (this->is_engaged()) {  // has_partner is implied
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell A");
        if (this->is_primary()) {
            //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell B");
            goal_ready = goal_ready || this->set_goal_cell_primary_engaged();
            if (!goal_ready) primary_engaged_and_moving = true;
            //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell C");
        } else {
            //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell D");
            goal_ready = goal_ready || this->set_goal_cell_secondary_engaged();
            //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell E");
        }
    } else if (this->is_ready_but_low_power()) {  // has_partner is implied
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell F");
        goal_ready = goal_ready || this->set_goal_cell_chill();
    }

    if (this->is_primary()) {  // unengaged, or engaged poorly (e.g. low power)
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell G");
        goal_ready = goal_ready || this->set_goal_cell_primary_intercept();
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell H");
    }
    else {  // secondary, has_partner is implied
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell I");
        goal_ready = goal_ready || this->set_goal_cell_secondary_rendezvous();
        //if (this->unit->_log_cond()) LUX_LOG("RB::goal_cell J");
    }

    //LUX_ASSERT(goal_ready);

    // Check for bad goal - shouldn't happen
    if (this->_goal_cell->opp_factory()) {
        LUX_LOG("WARNING: bad blockade goal cell " << *this->unit << ' ' << *this->_goal_cell);
        this->_goal_cell = opp_nonfactory_cell;
    }

    // Check for straightline condition
    if (!this->is_engaged()
        && this->has_target_unit()
        && this->target_unit->water >= 5
        && cur_cell->man_dist(this->_goal_cell) > 2
        && opp_cell->man_dist(this->_goal_cell) - 2 <= cur_cell->man_dist(this->_goal_cell)
        && this->unit->power_init - this->target_unit->power_init >= 10) {
        this->straightline = true;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade straightline");
    }

    // if engaged and primary and no good move
    //   and cell toward goal cell is par cell (and not swapping? / no threats?)
    if (primary_engaged_and_moving
        && cur_cell->neighbor_toward(this->_goal_cell) == this->partner->cell()) {
        this->_goal_cell = cur_cell;
        LUX_LOG(*this->unit << ' ' << *this->_goal_cell << " blockade primary chill actually1");
    }

    return this->_goal_cell;
}

void RoleBlockade::update_goal() {
    int cur_power = this->unit->power;

    if (this->goal_type == 'u') {  // Done with target unit goal?
        // Never transition back to factory
    } else if (this->goal_type == 'f') {  // Done with factory goal?
        int power_threshold = this->unit->cfg->BATTERY_CAPACITY - 3;
        if (cur_power >= power_threshold) {
            this->goal_type = 'u';
            this->goal = this->target_unit;
            return;
        }

        Cell *cur_cell = this->unit->cell();
        Cell *opp_cell = this->opp_cell();
        Cell *goal_cell = this->unengaged_goal_cell();

        int dist_c2f = cur_cell->man_dist(this->factory->cell);
        int dist_f2g = this->factory->cell->man_dist(goal_cell);
        int dist_c2g = cur_cell->man_dist(goal_cell);

        if (!this->has_target_unit()) {
            if (cur_power >= 120
                && 2 * dist_c2f >= opp_cell->man_dist_factory(this->target_factory)) {
                // maybe done
            } else {
                // keep charging up toward full capacity while there is no active water transporter
                return;
            }
        }

        int cost_c2g = board.naive_cost(this->unit, cur_cell, goal_cell);
        int cost_f2g = board.naive_cost(this->unit, this->factory->cell, goal_cell);

        int dist_diff = dist_c2f + dist_f2g - dist_c2g;
        int power_gain = 0;
        if (dist_diff > 0) {
            int start_step = board.sim_step + dist_c2g;
            power_gain = this->unit->power_gain(start_step, start_step+dist_diff);
        }

        if (150 - cost_f2g <= cur_power - cost_c2g + power_gain + 3) {
            vector<Cell*> temp;
            cost_c2g = board.pathfind(this->unit, cur_cell, goal_cell, NULL, NULL, NULL, &temp);
            dist_c2g = temp.size() - 1;
            cost_f2g = board.pathfind(this->unit, factory->cell, goal_cell, NULL, NULL, NULL, &temp);
            dist_f2g = temp.size() - 1;
            if (cost_c2g == INT_MAX || cost_f2g == INT_MAX) {
                LUX_LOG("WARNING: bad blockade pathfind " << *cur_cell << ' ' << *goal_cell);
                return;
            }

            dist_diff = dist_c2f + dist_f2g - dist_c2g;
            power_gain = 0;
            if (dist_diff > 0) {
                int start_step = board.sim_step + dist_c2g;
                power_gain = this->unit->power_gain(start_step, start_step+dist_diff);
            }

            if (150 - cost_f2g <= cur_power - cost_c2g + power_gain + 3) {
                // Go to goal now
                this->goal_type = 'u';
                this->goal = this->target_unit;
                return;
            }
        }

        if (this->has_target_unit()
            && this->target_unit->water >= 5) {
            int opp_power = this->target_unit->power_init;
            for (int i = 0; i < MIN(5, this->target_unit->aq_len); i++) {
                ActionSpec &spec = this->target_unit->action_queue[0];
                if (spec.action == UnitAction_PICKUP
                    && spec.resource == Resource_POWER) {
                    opp_power = MIN(150, opp_power + spec.amount);
                }
            }

            int pickup_dist = (cur_cell->man_dist_factory(this->factory)
                               + 1
                               + this->factory->cell->man_dist(goal_cell));

            // Not enough time to get more power
            if (pickup_dist >= opp_cell->man_dist_factory(this->target_factory)
                && cur_power >= opp_power) {
                this->goal_type = 'u';
                this->goal = this->target_unit;
                return;
            }
        }
    } else {
        LUX_ASSERT(false);
    }
}

bool RoleBlockade::do_move() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_move A");

    if (this->_do_move()) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_move B");
        return true;
    }

    // Lock in no-move if opp exists and is within 20
    if (this->has_target_unit()
        && (this->unit->cell()->man_dist(this->opp_cell()) <= 20
            || (this->has_partner()
                && this->partner->cell()->man_dist(this->opp_cell()) <= 20))) {
        //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_move C");
        return this->_do_no_move();
    }

    //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_move D");
    return false;
}

bool RoleBlockade::do_dig() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_dig A");
    return false;
}

bool RoleBlockade::do_transfer() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_transfer A");
    return this->_do_transfer_resource_to_factory();
}

bool RoleBlockade::do_pickup() {
    //if (this->unit->_log_cond()) LUX_LOG("RoleBlockade::do_pickup A");
    return this->_do_power_pickup();
}
