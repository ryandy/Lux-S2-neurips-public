#include "lux/cell.hpp"

#include <climits>

#include "agent.hpp"
#include "lux/board.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/player.hpp"
#include "lux/team.hpp"
using namespace std;


void Cell::init(int16_t cell_id, int16_t _x, int16_t _y, bool _ice, bool _ore, int8_t _rubble) {
    this->id = cell_id;
    this->x = _x;
    this->y = _y;
    this->ice = _ice;
    this->ore = _ore;
    this->rubble = _rubble;
    this->lichen = 0;
    this->lichen_strain = -1;
    this->valid_spawn = false;
    this->ice1_spawn = false;
    this->factory_center = false;
    this->factory = NULL;
    this->flood_fill_call_id = 0;
    this->path_info = {};
    this->unit = NULL;
    this->unit_next = NULL;
    this->assigned_unit = NULL;
    this->assigned_factory = NULL;
    this->flatland_id = -1;
    this->flatland_size = -1;
    this->lowland_id = -1;
    this->lowland_size = -1;
    this->iceland_id = -1;
    this->iceland_size = -1;
    this->step0_score = INT_MIN;
    this->_ice_vulnerable_cells_ready = false;
    this->_is_contested_step = -1;
    this->future_heavy_dig_step = INT_MAX;
    this->future_light_dig_step = INT_MAX;
    this->lichen_connected_step = -1;
    this->lichen_opp_boundary_step = -1;
    this->lichen_frontier_step = -1;
    this->lichen_bottleneck_step = -1;
    this->lichen_dist = INT_MAX;
}

void Cell::init_neighbors() {
    this->neighbors_plus.push_back(this);

    for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	    Cell *neighbor = this->neighbor(dx, dy);
	    if (abs(dx) + abs(dy) == 1 && neighbor != NULL) {
		this->neighbors.push_back(neighbor);
		this->neighbors_plus.push_back(neighbor);
	    }
	}
    }

    this->north = this->neighbor(Direction_NORTH);
    this->east = this->neighbor(Direction_EAST);
    this->south = this->neighbor(Direction_SOUTH);
    this->west = this->neighbor(Direction_WEST);
}

void Cell::reinit_rubble(int8_t _rubble) {
    this->rubble = _rubble;
}

void Cell::reinit_lichen(int8_t _lichen) {
    this->lichen = _lichen;
}

void Cell::reinit_lichen_strain(int8_t _lichen_strain) {
    this->lichen_strain = _lichen_strain;
}

void Cell::save_begin() {
    // Save these values at beginning of step 0 simulation because they will be updated via diff.
    this->_save_rubble = this->rubble;
    this->_save_lichen = this->lichen;
    this->_save_lichen_strain = this->lichen_strain;
}

void Cell::save_end() {
    this->_save_assigned_factory = this->assigned_factory;
}

void Cell::load() {
    this->unit = NULL;
    this->unit_next = NULL;
    this->rubble = this->_save_rubble;
    this->lichen = this->_save_lichen;
    this->lichen_strain = this->_save_lichen_strain;
    this->assigned_factory = this->_save_assigned_factory;
}

Factory *Cell::own_factory(Player *player) {
    player = player ? player : board.player;
    return (this->factory && this->factory->player == player) ? this->factory : NULL;
}

Factory *Cell::opp_factory(Player *player) {
    player = player ? player : board.player;
    return (this->factory && this->factory->player->team != player->team) ? this->factory : NULL;
}

void Cell::update_unit_history(struct Unit *_unit) {
    this->_unit_history[board.step % 100] = _unit;
}

Unit *Cell::get_unit_history(int step, Player *player) {
    LUX_ASSERT(0 <= step && board.step - 100 < step && step <= board.step);
    Unit *_unit = this->_unit_history[step % 100];
    return (_unit && (player == NULL || _unit->player == player)) ? _unit : NULL;
}

// Returns current unit for player, otherwise i=0 unit
Unit *Cell::own_unit(Player *player) {
    player = player ? player : board.player;
    Unit *_unit = (player == board.player) ? this->unit : this->get_unit_history(board.step);
    return (_unit && _unit->player == player) ? _unit : NULL;
}

// Always returns i=0 unit
Unit *Cell::opp_unit(Player *player) {
    player = player ? player : board.player;
    return this->get_unit_history(board.step, (player == board.player ? board.opp : board.player));
}

bool Cell::is_surrounded() {
    for (Cell *neighbor : this->neighbors) {
        if (!neighbor->own_unit()) return false;
    }
    return true;
}

bool Cell::is_contested() {
    // Check for cached value
    if (board.step / 100 == this->_is_contested_step / 100) return this->_is_contested;

    this->_is_contested = false;
    this->_is_contested_step = board.step;

    Factory *f0 = this->nearest_factory(board.player);
    Factory *f1 = this->nearest_factory(board.opp);
    LUX_ASSERT(f0);
    LUX_ASSERT(f1);

    int f0_dist = this->man_dist_factory(f0);
    int f1_dist = this->man_dist_factory(f1);

    if (MIN(f0_dist, f1_dist) <= 8 && abs(f0_dist - f1_dist) <= 4) {
        int f0_cost = board.pathfind(this, f0->cell, [&](Cell *c) { return c->factory == f0; });
        int f1_cost = board.pathfind(this, f1->cell, [&](Cell *c) { return c->factory == f1; });
        if (abs(f0_cost - f1_cost) <= 180) this->_is_contested = true;
    }

    return this->_is_contested;
}

Cell *Cell::neighbor(int dx, int dy) {
    return board.cell(this->x + dx, this->y + dy);
}

Cell *Cell::neighbor(Direction direction) {
    return this->neighbor(direction_x(direction), direction_y(direction));
}

Cell *Cell::neighbor_toward(Cell *other_cell) {
    int dx = other_cell->x - this->x;
    int dy = other_cell->y - this->y;
    if (abs(dx) > abs(dy)) return this->neighbor(dx / abs(dx), 0);
    if (dy != 0) return this->neighbor(0, dy / abs(dy));
    return this;
}

Direction Cell::neighbor_to_direction(struct Cell *neighbor) {
    if (neighbor->y < this->y) return Direction_NORTH;
    if (neighbor->x > this->x) return Direction_EAST;
    if (neighbor->y > this->y) return Direction_SOUTH;
    if (neighbor->x < this->x) return Direction_WEST;
    return Direction_CENTER;
}

bool Cell::is_between(Cell *a, Cell *b) {
    return (((a->x <= this->x && this->x <= b->x)
             || (a->x >= this->x && this->x >= b->x))
            && ((a->y <= this->y && this->y <= b->y)
                || (a->y >= this->y && this->y >= b->y)));
}

Cell *Cell::radius_cell(int max_radius, Cell *prev_cell) {
    return this->radius_cell(0, max_radius, prev_cell);
}

Cell *Cell::radius_cell(int min_radius, int max_radius, Cell *prev_cell) {
    LUX_ASSERT(0 <= min_radius);
    LUX_ASSERT(min_radius <= max_radius);

    int dx, dy;
    Cell *next_cell = NULL;
    if (prev_cell) {
        dx = prev_cell->x - this->x;
        dy = prev_cell->y - this->y;
    } else {
        dx = -min_radius;
        dy = 0;
        next_cell = this->neighbor(dx, dy);  // First cell
    }

    int radius = abs(dx) + abs(dy);
    while (!next_cell) {
        if (dy > 0) {  // First try "incrementing" dy
            dy *= -1;
        }
        else if (dx < radius) {  // Next try incrementing dx
            dx += 1;
            dy = radius - abs(dx);
        }
        else if (radius < max_radius) {  // Next try incrementing radius
            radius += 1;
            dx = -radius;
            dy = 0;
        }
        else {  // No more radius cells
            break;
        }
        next_cell = this->neighbor(dx, dy);
    }

    return next_cell;
}

Cell *Cell::radius_cell_factory(int max_radius, Cell *prev_cell) {
    return this->radius_cell_factory(1, max_radius, prev_cell);
}

Cell *Cell::radius_cell_factory(int min_radius, int max_radius, Cell *prev_cell) {
    LUX_ASSERT(1 <= min_radius);
    LUX_ASSERT(min_radius <= max_radius);

    int dx, dy, radius;
    Cell *next_cell = NULL;
    if (prev_cell) {
        radius = this->man_dist_factory(prev_cell) + 2;
        dx = prev_cell->x - this->x;
        dy = prev_cell->y - this->y;
        if (dx == 0) dy += (dy < 0) ? -1 : 1;
        else if (dy == 0) dx += (dx < 0) ? -1 : 1;
    } else {
        radius = min_radius + 2;
        dx = -radius;
        dy = 0;
        next_cell = this->neighbor(dx + 1, dy);  // First cell
    }

    // Basically normal radius_cells but add 2 to radius and squeeze in the 4 cardinal points
    while (!next_cell) {
        if (dy > 0) {  // First try "incrementing" dy
            dy *= -1;
        }
        else if (dx < radius) {  // Next try incrementing dx
            dx += 1;
            dy = radius - abs(dx);
        }
        else if (radius < max_radius + 2) {  // Next try incrementing radius
            radius += 1;
            dx = -radius;
            dy = 0;
        }
        else {  // No more radius cells
            break;
        }

        if (dx && dy) next_cell = this->neighbor(dx, dy);
        else if (dy == 0 && dx < 0) next_cell = this->neighbor(dx + 1, dy);
        else if (dy == 0 && dx > 0) next_cell = this->neighbor(dx - 1, dy);
        else if (dx == 0 && dy < 0) next_cell = this->neighbor(dx, dy + 1);
        else if (dx == 0 && dy > 0) next_cell = this->neighbor(dx, dy - 1);
        else { LUX_ASSERT(false); }
    }

    return next_cell;
}

int Cell::man_dist_factory(Factory *_factory) const {
    LUX_ASSERT(_factory);
    return this->man_dist_factory(_factory->cell);
}

int Cell::man_dist_factory(Cell *other) const {
    LUX_ASSERT(other);
    int dx = MAX(0, abs(this->x - other->x) - 1);
    int dy = MAX(0, abs(this->y - other->y) - 1);
    return dx + dy;
}

Factory *Cell::_nearest_factory(Player *player, Factory *ignore_factory) {  // default: any factory
    int nearest_dist = INT_MAX;
    Factory *nearest_factory = NULL;
    for (Factory &_factory : board.factories) {
        if (_factory.alive()
            && (player == NULL || _factory.player == player)) {
            if (ignore_factory == &_factory) continue;
            int dist = this->man_dist_factory(&_factory);
            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest_factory = &_factory;
            }
        }
    }
    return nearest_factory;
}

Factory *Cell::nearest_factory(Player *player) {  // default: any factory
    // NOTE: Would need to change for multi-player teams
    if (player->team == &board.home) return this->nearest_home_factory;
    if (player->team == &board.away) return this->nearest_away_factory;
    return ((this->home_dist < this->away_dist)
            ? this->nearest_home_factory
            : this->nearest_away_factory);
}

int Cell::nearest_factory_dist(Player *player) {
    return this->man_dist_factory(this->nearest_factory(player));
}

void Cell::update_factory_dists() {
    // TODO: only recalculate if prev factory is destroyed?
    this->nearest_home_factory = this->_nearest_factory(board.player);
    this->nearest_away_factory = this->_nearest_factory(board.opp);
    // Note: nearest factory may not exist during bidding/placement
    this->home_dist = (this->nearest_home_factory
                       ? this->man_dist_factory(this->nearest_home_factory)
                       : 2 * SIZE);
    this->away_dist = (this->nearest_away_factory
                       ? this->man_dist_factory(this->nearest_away_factory)
                       : 2 * SIZE);
}

void Cell::set_unit_assignment(Unit *_unit) {
    if (this->assigned_unit) {
        LUX_LOG("Error: set cell->unit: " << *this << ' ' << *this->assigned_unit << ' ' << *_unit);
    }
    this->assigned_unit = _unit;
}

void Cell::unset_unit_assignment(Unit *_unit) {
    if (this->assigned_unit != _unit) {
        LUX_LOG("Error: unset cell->unit " << *this << ' ' << *this->assigned_unit << ' ' << *_unit);
    }
    this->assigned_unit = NULL;
}

// Returns value roughly equal to 1 per resource cell in neighbors_plus
double Cell::get_antagonize_score(bool heavy) {
    double score = 0;
    for (Cell *neighbor : this->neighbors_plus) {
        if (neighbor->ice || neighbor->ore) {
            PDD traffic_score = neighbor->get_traffic_score();
            if (heavy) {
                // heavy traffic is good
                // light traffic is ~0.1 as good
                score += 1 + (4.0 * traffic_score.first) + (0.1 * traffic_score.second);
            } else {  // light
                // light traffic is good
                // heavy traffic is bad (negative)
                score += 1 + (-4.0 * traffic_score.first) + (1.0 * traffic_score.second);
            }
        }
    }
    return score;
}

// Returns heavy/light pair: 1.0 for constant presence, 0.0 for none, etc
PDD Cell::get_traffic_score(Player *player, bool include_neighbors) {
    player = player ? player : board.opp;

    double heavy_score = 0;
    double light_score = 0;
    if (include_neighbors) {
        for (Cell *neighbor : this->neighbors_plus) {
            PDD score = neighbor->get_traffic_score(player, false);
            heavy_score += score.first;
            light_score += score.second;
        }
        return make_pair(heavy_score / this->neighbors_plus.size(),
                         light_score / this->neighbors_plus.size());
    }

    int history_len = 50;
    for (int step = board.step; step >= MAX(0, board.step - history_len); step--) {
        Unit *u = this->get_unit_history(step, player);
        if (u && u->heavy) heavy_score += 1;
        else if (u) light_score += 1;
    }

    return make_pair(heavy_score / history_len, light_score / history_len);
}

int _get_dist_to_nearest(int *darray, int max_radius, int nearest = 1) {
    int count = 0;
    for (int dist = 1; dist <= max_radius; dist++) {
        count += darray[dist];
        if (count >= nearest) return dist;
    }
    return max_radius + 5;
}

double Cell::get_spawn_score(double ore_mult, bool verbose) {
    static int const MAX_RADIUS = 20;
    int dice[MAX_RADIUS+1] = {0};
    int dore[MAX_RADIUS+1] = {0};
    int dflat[MAX_RADIUS+1] = {0};
    int dlow[MAX_RADIUS+1] = {0};
    int dice_all[MAX_RADIUS+1] = {0};
    int dore_all[MAX_RADIUS+1] = {0};

    static int const ICE_CELLS_MAX_LEN = 20;
    Cell *ice_cells[ICE_CELLS_MAX_LEN] = {NULL}; int ice_idx = 0;
    Cell *ice1_spawn_cells[ICE_CELLS_MAX_LEN] = {NULL}; int ice1_spawn_idx = 0;

    int ice3_count = 0;  // dist 3 from center (max 3)
    int ice5_count = 0;  // dist 5 from factory (max 3)
    int ore5_count = 0;  // dist 5 from factory (max 1)

    Cell *cell = this->radius_cell_factory(MAX_RADIUS);
    while (cell) {
        int dist_center = cell->man_dist(this);
        int dist = cell->man_dist_factory(this);
        LUX_ASSERT(dist > 0);

        if (dist_center >= 7 && cell->valid_spawn && cell->ice1_spawn) {
            ice1_spawn_idx = MIN(ICE_CELLS_MAX_LEN-1, ice1_spawn_idx);
            ice1_spawn_cells[ice1_spawn_idx++] = cell;
        }

        if (cell->ice) {
            dice_all[dist] += 1;
            ice_idx = MIN(ICE_CELLS_MAX_LEN-1, ice_idx);
            ice_cells[ice_idx++] = cell;
        }

        if (cell->ore) {
            dore_all[dist] += 1;
        }

        // In general, don't count ore/ice/low/flat if it's dist1 to opp
        if (cell->away_dist == 1) {
            cell = this->radius_cell_factory(MAX_RADIUS, cell);
            continue;
        }

        if (cell->ore) {
            dore[dist] += 1;
            if (dist <= 5) ore5_count = MIN(1, ore5_count + 1);
        }

        // In general, don't count ice/low/flat if it's closer to other factory
        if (dist > MIN(cell->away_dist, cell->home_dist)) {
            cell = this->radius_cell_factory(MAX_RADIUS, cell);
            continue;
        }

        if (cell->ice) {
            dice[dist] += 1;
            if (dist_center <= 3) ice3_count = MIN(1, ice3_count + 1);
            if (dist <= 5) ice5_count = MIN(1, ice5_count + 1);
        }

        if (cell->rubble == 0 && !cell->ice && !cell->ore) dflat[dist] += 1;
        if (cell->rubble < 20 && !cell->ice && !cell->ore) dlow[dist] += 1;

        cell = this->radius_cell_factory(MAX_RADIUS, cell);
    }

    int ice1_dist = _get_dist_to_nearest(dice, 3);
    int ice2_dist = _get_dist_to_nearest(dice, 5, 2);
    int ore1_dist = _get_dist_to_nearest(dore, 5);
    int ore1_dist_long = _get_dist_to_nearest(dore, 20);

    double low_weighted = 0;
    static double const decay[] = {
        0,1,0.7142857142857143,0.5442176870748299,0.431918799265738,0.3525867749108065,
        0.293822312425672,0.24873846554554246,0.2132043990390364,0.184592553280551,0.1611522290544493,
        0.14167228927863673,0.12528841908995084,0.11136748363551185,0.09943525324599271};
    for (int radius = 1; radius <= MAX_RADIUS; radius++) {
        low_weighted += dlow[radius] * decay[radius];
        if (ice5_count <= 1 && radius >= 8) break;
    }

    // ~ secure_other_factory_bonus ~
    // Small bonus for placing in such a way as to prevent an IC attack on a friendly factory
    int secure_other_factory_bonus = 0;
    if (board.step > 0) {
        for (Factory *factory : board.player->factories()) {
            vector<Cell*> &ice_vuln_cells = factory->cell->ice_vulnerable_cells();
            if (!ice_vuln_cells.empty()) {
                bool all_close = true;
                for (Cell *ice_vuln_cell : ice_vuln_cells) {
                    if (ice_vuln_cell->man_dist(factory->cell) >= 7) {
                        all_close = false;
                        break;
                    }
                }
                if (all_close) {
                    secure_other_factory_bonus = 1;
                    break;
                }
            }
        }
    }

    // ~ ice_conflict_bonus ~
    // Large bonus for late factories if they can pressure an opp factory's ice
    double ice_conflict_bonus = 0;
    double desperate_ice_conflict_bonus = 0;
    int remaining_factories = agent.factories_per_team - ((board.step - 1) / 2);
    int max_ice_vuln_count = agent.factories_per_team / 2;
    if (this->away_dist <= 10
        && !this->nearest_away_factory->_ice_vuln_covered
        && ((board._ice_vuln_count + remaining_factories <= max_ice_vuln_count)
            || (board.step >= 2 * agent.factories_per_team - 1))) {
        vector<Cell*> &ice_vuln_cells = this->nearest_away_factory->cell->ice_vulnerable_cells();
        if (count(ice_vuln_cells.begin(), ice_vuln_cells.end(), this)) {

            // Check dists to existing opp/own factories from the target factory
            Factory *nearest_opp_factory = this->nearest_away_factory->cell->_nearest_factory(
                board.opp, this->nearest_away_factory);
            Factory *nearest_own_factory = this->nearest_away_factory->cell->_nearest_factory(
                board.player);
            int opp_factory_dist = (
                nearest_opp_factory
                ? this->nearest_away_factory->cell->man_dist_factory(nearest_opp_factory)
                : 100);
            int own_factory_dist = (
                nearest_own_factory
                ? this->nearest_away_factory->cell->man_dist_factory(nearest_own_factory)
                : 100);
            opp_factory_dist = MIN(35, opp_factory_dist);
            own_factory_dist = MIN(35, own_factory_dist);

            int ice_dist = (
                nearest_opp_factory
                ? nearest_opp_factory->ice_cells[0]->man_dist_factory(nearest_opp_factory)
                : 100);
            int ore_dist = (
                nearest_opp_factory
                ? nearest_opp_factory->ore_cells[0]->man_dist_factory(nearest_opp_factory)
                : 100);
            int ore1_bonus = (ice_dist == 1 && ore_dist == 1) ? 1 : 0;
            int ore3_bonus = (!ore1_bonus && ice_dist == 1 && ore_dist <= 3) ? 1 : 0;
            int ore5_bonus = (!ore1_bonus && !ore3_bonus && ice_dist == 1 && ore_dist <= 5) ? 1 : 0;

            // Ice conflicts are best when opp is not too close and a friend is not too far
            desperate_ice_conflict_bonus = (
                1
                + 0.0010 * opp_factory_dist
                - 0.0005 * own_factory_dist
                + 0.1 * ore1_bonus
                + 0.05 * ore3_bonus
                + 0.025 * ore5_bonus
                + (0.08 - 0.01 * this->away_dist));
            if (opp_factory_dist >= 15 && own_factory_dist < 35) {
                ice_conflict_bonus = desperate_ice_conflict_bonus;
            }

            // Allow over-limit ice conflicts for last placement, but reduce bonus
            if (board._ice_vuln_count + remaining_factories > max_ice_vuln_count) {
                ice_conflict_bonus /= 2.0;
                desperate_ice_conflict_bonus /= 2.0;
            } else if (opp_factory_dist < 10) {
                desperate_ice_conflict_bonus /= 2.0;
            }
        }
    }

    // ~ desperate_ice_dist ~
    // If not close to ice and not a real ice conflict, at least be close to an opp's ice
    int desperate_ice_dist = 0;
    if (!ice3_count
        && !ice_conflict_bonus
        && !desperate_ice_conflict_bonus
        && this->nearest_away_factory) {
        Factory *opp_factory = this->nearest_away_factory;
        int min_opp_ice_dist = opp_factory->ice_cells[0]->man_dist_factory(opp_factory);
        int max_own_ice_dist = 0;
        for (Cell *ice_cell : opp_factory->ice_cells) {
            if (ice_cell->man_dist_factory(opp_factory) > min_opp_ice_dist) break;
            int own_ice_dist = ice_cell->man_dist_factory(this);

            // Discourage picking on an opp factory that is hovering near my own low-ice factory
            Factory *own_factory = ice_cell->nearest_home_factory;
            if (own_factory
                && own_factory->ice_cells[0] == ice_cell
                && (own_factory->ice_cells[0]->man_dist_factory(own_factory)
                    < own_factory->ice_cells[1]->man_dist_factory(own_factory))) {
                own_ice_dist += 15;
            }

            if (own_ice_dist > max_own_ice_dist) {
                max_own_ice_dist = own_ice_dist;
            }
        }
        desperate_ice_dist = max_own_ice_dist;
    }

    // ~ oasis_bonus ~
    // No ice-adj factory locations available within X dist of at least 1 nearby ice
    int oasis_bonus = 0;
    if (ice1_dist <= 2 && this->away_dist >= 8) {
        for (Cell *near_ice_cell : ice_cells) {
            bool isolated_ice = true;
            if (!near_ice_cell || this->man_dist_factory(near_ice_cell) > 2) break;
            if (near_ice_cell->away_dist < 12) isolated_ice = false;
            for (Cell *ice1_spawn_cell : ice1_spawn_cells) {
                if (!ice1_spawn_cell || !isolated_ice) break;
                if (ice1_spawn_cell->man_dist_factory(near_ice_cell) < 12) isolated_ice = false;
            }
            if (isolated_ice) {
                //LUX_LOG(*this << " oasis");
                oasis_bonus = 1;
                break;
            } else {
                //if (this->x == 43 && this->y == 28)
                //    LUX_LOG(*this << " non-iso ice: " << *near_ice_cell);
            }
        }
    }

    bool ice1_bonus = dice[1];
    bool ore1_bonus = dore[1];
    bool ore5_bonus = ore5_count;
    int ice1_mult = 1;
    int ice2_mult = 1;

    if (ice1_bonus || ice_conflict_bonus) {
        desperate_ice_conflict_bonus = 0;
    }

    if (ice_conflict_bonus || desperate_ice_conflict_bonus) {
        ice1_mult = 3;
        ore_mult = 0.5;
    }

    if (board.low_iceland()) {
        ice2_mult = 3;
        ore_mult = 0.25;
    }

    if (ice_conflict_bonus || desperate_ice_conflict_bonus) {
        ice1_dist = _get_dist_to_nearest(dice_all, 3);
        ice2_dist = _get_dist_to_nearest(dice_all, 5, 2);
        ore1_dist = _get_dist_to_nearest(dore_all, 5);
        ore1_dist_long = _get_dist_to_nearest(dore_all, 20);
    }

    PDD features[] = {
        make_pair(ice1_bonus, 300),
        make_pair(ore1_bonus, 15 * ore_mult),
        make_pair(ore5_bonus, 20 * ore_mult),
        make_pair(ice1_dist, -6 * ice1_mult),
        make_pair(ice2_dist, -3.5 * ice2_mult),
        make_pair(ore1_dist, -5.5),
        make_pair(ore1_dist_long, -1.5 * ore_mult),
        make_pair(MIN(50, low_weighted), 2.5),
        make_pair(low_weighted, 0.01),
        make_pair(oasis_bonus, 20),
        make_pair(ice_conflict_bonus, 500),
        make_pair(desperate_ice_conflict_bonus, 300),
        make_pair(desperate_ice_dist, -100),
        make_pair(secure_other_factory_bonus, 1),
    };

    double score = 1000;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
        score += features[i].first * features[i].second;
    }

    // Debug print:
    //if (verbose || (this->x == 36 && this->y == 21)) {
    if (verbose) {
        LUX_LOG(*this);
        for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
            LUX_LOG("  " << features[i].first << " * " << features[i].second
                    << " = " << features[i].first * features[i].second);
        }
        LUX_LOG("  TOTAL = " << score);
    }

    return score;
}

double Cell::get_spawn_security_score() {
    int ice_security_bonus = this->ice_vulnerable() ? 0 : 1;
    //if (board.step == 0 && this->ice_vulnerable()) LUX_LOG("Vuln: " << *this);
    return ice_security_bonus * 4;
}

// If an ice_vuln cell is currently spawnable, then this cell is generally vulnerable
bool Cell::ice_vulnerable() {
    vector<Cell*> &ice_vulnerable_cells = this->ice_vulnerable_cells();
    for (Cell *cell : ice_vulnerable_cells) {
        if (cell->valid_spawn) return true;
    }
    return false;
}

// Is a factory at this cell ice-conflict-vulnerable to a factory at other_cell?
bool Cell::ice_vulnerable(Cell *other_factory_cell) {
    vector<Cell*> ice_cells;
    Cell *cell = this->radius_cell_factory(ICE_VULN_CHECK_RADIUS);

    // Check move dist of ice cells to both factory locations
    while (cell) {
        if (cell->ice) {
            ice_cells.push_back(cell);
            int this_dist = cell->man_dist_factory(this);
            int other_dist = cell->man_dist_factory(other_factory_cell);
            // If ice_cell is ~5+ closer to this factory than other_factory -> not vulnerable
            if (this_dist <= other_dist - ICE_VULN_DIST_THRESHOLD) {
                return false;
            }
        }
        cell = this->radius_cell_factory(ICE_VULN_CHECK_RADIUS, cell);
    }

    // Check move cost of ice cells to both factory locations
    for (Cell *ice_cell : ice_cells) {
        int this_cost = board.pathfind(
            ice_cell, this,
            [&](Cell *c) { return c->man_dist_factory(this) == 0; },
            [&](Cell *c) { return c->factory; });
        //LUX_ASSERT(this_cost != INT_MAX);
        int other_cost = board.pathfind(
            ice_cell, other_factory_cell,
            [&](Cell *c) { return c->man_dist_factory(other_factory_cell) == 0; },
            [&](Cell *c) { return c->factory; });
        //LUX_ASSERT(other_cost != INT_MAX);

        // Shouldn't happen and yet..
        if (this_cost == INT_MAX || other_cost == INT_MAX) {
            LUX_LOG("WARNING: bad IC pathfind " << *ice_cell << ' ' << *this << ' ' << this_cost
                    << ' ' << *other_factory_cell << ' ' << other_cost);
            return false;
        }

        if (this_cost <= other_cost - ICE_VULN_COST_THRESHOLD) {
            return false;
        }
    }

    // What if ice_cells.empty()? (i.e. no ice cells near this) return false?
    if (ice_cells.empty()) return false;

    // No ice cell exists that proves this factory is ice-secure -> so ice-vulnerable to other_cell.
    return true;
}

vector<Cell*> &Cell::ice_vulnerable_cells() {
    if (!this->_ice_vulnerable_cells_ready) {
        board.flood_fill(
            this,
            [&](Cell *c) {
                int dist = this->man_dist(c);
                bool ret = (dist < MIN_FACTORY_DIST || this->ice_vulnerable(c));
                if (ret && dist >= MIN_FACTORY_DIST)
                    this->_ice_vulnerable_cells.push_back(c);
                return ret;
            });
        this->_ice_vulnerable_cells_ready = true;
    }

    return this->_ice_vulnerable_cells;
}
