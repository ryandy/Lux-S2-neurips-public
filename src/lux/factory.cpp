#include "lux/factory.hpp"

#include <algorithm>  // stable_sort
#include <set>

#include "lux/action.hpp"
#include "lux/board.hpp"
#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode.hpp"
#include "lux/role.hpp"
#include "lux/role_miner.hpp"
#include "lux/unit.hpp"
using namespace std;


// Can be called multiple times (placement steps and step 0)
void Factory::init(int factory_id, int _player_id, int _x, int _y, int _water, int _metal) {
    this->id = factory_id;
    this->player = board.get_player(_player_id);
    this->x = _x;
    this->y = _y;
    this->ice = 0;
    this->ore = 0;
    this->water = _water;
    this->metal = _metal;
    this->power = INIT_POWER_PER_FACTORY;
    this->mode = NULL;
    this->_save_mode = NULL;

    this->cell = board.cell(_x, _y);
    this->alive_step = board.step;
    this->last_action_step = -1;

    // Only run once:
    if (!this->cell->factory_center) {
        this->cell->factory_center = true;
        this->heavy_ice_miner_count = 0;
        this->heavy_ore_miner_count = 0;
        this->heavy_antagonizer_count = 0;
        this->inbound_water_transporter_count = 0;

        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                Cell *_cell = board.cell(this->x + dx, this->y + dy);
                _cell->rubble = 0;
                _cell->factory = this;
                this->cells_plus.push_back(_cell);  // includes center cell
                if (dx || dy) this->cells.push_back(_cell);  // do not include center cell
            }
        }
        this->ice_cells = board.ice_cells;
        this->ore_cells = board.ore_cells;
        stable_sort(this->ice_cells.begin(), this->ice_cells.end(),
                    [&](const Cell *a, const Cell *b) {
                        return a->man_dist_factory(this) < b->man_dist_factory(this); });
        stable_sort(this->ore_cells.begin(), this->ore_cells.end(),
                    [&](const Cell *a, const Cell *b) {
                        return a->man_dist_factory(this) < b->man_dist_factory(this); });
    }
}

void Factory::reinit(int _ice, int _ore, int _water, int _metal, int _power) {
    this->ice = _ice;
    this->ore = _ore;
    this->water = _water;
    this->metal = _metal;
    this->power = _power;
    this->alive_step = board.step;
    this->last_action_step = -1;
}

void Factory::save_end() {
    this->_save_mode = this->mode->copy();
}

void Factory::load() {
    if (this->mode) delete this->mode;
    this->mode = this->_save_mode;
}

void Factory::handle_destruction() {
    if (this->mode) {
        LUX_LOG("died: " << *this << ' ' << *this->mode);
    } else {
        LUX_ASSERT(this->player == board.opp);
        LUX_LOG("died: " << *this);
    }

    this->cell->factory_center = false;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            board.cell(this->x + dx, this->y + dy)->factory = NULL;
            board.cell(this->x + dx, this->y + dy)->factory_center = false;
        }
    }
    for (Unit *unit : board.player->units()) {
        if (unit->assigned_factory == this) unit->assigned_factory = NULL;
    }
    for (Cell &_cell : board.cells) {
        if (_cell.assigned_factory == this) _cell.assigned_factory = NULL;
    }
}

bool Factory::alive() {
    return this->alive_step == board.step;
}

string Factory::id_str() {
    return "factory_" + to_string(this->id);
}

Cell *Factory::cell_toward(Cell *other_cell) {
    LUX_ASSERT(other_cell);

    // Inside factory
    if (other_cell->factory == this) return other_cell;

    // Diagonal
    if (other_cell->y < this->y && other_cell->x < this->x) return this->cell->north->west;
    if (other_cell->y < this->y && other_cell->x > this->x) return this->cell->north->east;
    if (other_cell->y > this->y && other_cell->x < this->x) return this->cell->south->west;
    if (other_cell->y > this->y && other_cell->x > this->x) return this->cell->south->east;

    // Vertical/horizontal
    if (other_cell->x == this->x) {
        if (other_cell->y < this->y) return this->cell->north;
        if (other_cell->y > this->y) return this->cell->south;
    }
    if (other_cell->y == this->y) {
        if (other_cell->x < this->x) return this->cell->west;
        if (other_cell->x > this->x) return this->cell->east;
    }

    LUX_ASSERT(false);
    return this->cell;
}

Cell *Factory::neighbor_toward(struct Cell *other_cell) {
    LUX_ASSERT(other_cell->factory != this);
    return this->cell_toward(other_cell)->neighbor_toward(other_cell);
}

Cell *Factory::radius_cell(int max_radius, Cell *prev_cell) {
    return this->cell->radius_cell_factory(max_radius, prev_cell);
}

Cell *Factory::radius_cell(int min_radius, int max_radius, Cell *prev_cell) {
    return this->cell->radius_cell_factory(min_radius, max_radius, prev_cell);
}

void Factory::new_mode(Mode *new_mode) {
    if (this->mode) {
        if (board.sim0()) LUX_LOG("X! " << *this << ' ' << *this->mode);
        this->delete_mode();
    }
    this->mode = new_mode;
    if (board.sim0()) LUX_LOG("   " << *this << ' ' << *this->mode);
    this->mode->set();
}

void Factory::delete_mode() {
    if (board.sim0()) LUX_LOG("X  " << *this << ' ' << *this->mode);
    this->mode->unset();
    delete this->mode;
    this->mode = NULL;
}

int Factory::power_reserved() {
    // TODO: also count ore/metal in miners/transporters
    int _metal = this->metal + this->metal_delta + (this->ore + this->ore_delta) / ORE_METAL_RATIO;
    if (_metal >= g_heavy_cfg.METAL_COST) {
        return g_heavy_cfg.INIT_POWER;
    }
    if (this->heavy_ore_miner_count) {
        return g_heavy_cfg.INIT_POWER * _metal / g_heavy_cfg.METAL_COST;
    }
    return 0;
}

double Factory::power_usage(Unit *skip_unit) {
    double unit_power_usage = 0;
    for (Unit *unit : this->units) {
        if (unit != skip_unit) {
            // TODO: move to unit->power_usage() (can incorporate recent historical data)
            if (unit->role) unit_power_usage += unit->role->power_usage();
            else unit_power_usage += 1.5 * unit->cfg->MOVE_COST;
        }
    }

    double unit_power_gain = (6 * this->heavies.size() + 0.6 * this->lights.size());
    if (skip_unit && skip_unit->assigned_factory == this) {
        unit_power_gain -= (skip_unit->heavy ? 6 : 0.6);
    }

    return unit_power_usage - unit_power_gain;
}

int Factory::power_gain() {
    return FACTORY_CHARGE + this->lichen_connected_count * POWER_PER_CONNECTED_LICHEN_TILE;
}

double Factory::water_income(Unit *skip_unit) {
    double water_income = 0;
    for (Unit *unit : this->units) {
        if (unit != skip_unit) {
            RoleMiner *role_miner = RoleMiner::cast(unit->role);
            if (role_miner) water_income += role_miner->water_income();
        }
    }
    return water_income;
}

// Also updates cell->lichen_opp_boundary_step
void Factory::update_lichen_info(bool is_begin_step) {
    this->lichen_connected_cells.clear();
    this->lichen_growth_cells.clear();
    this->lichen_flat_boundary_cells.clear();
    this->lichen_rubble_boundary_cells.clear();
    this->lichen_frontier_cells.clear();

    auto cell_cond = [&](Cell *c) {
        // Within factory
        if (c->factory == this) return true;

        // Connected lichen area
        if (c->lichen_strain == this->id) {
            if (board.sim0()) c->lichen_connected_step = board.step;
            this->lichen_connected_cells.push_back(c);
            this->lichen_growth_cells.push_back(c);
            return true;
        }

        // Beyond
        if (this->player == board.player
            && board.opp->is_strain(c->lichen_strain)) {
            c->lichen_opp_boundary_step = board.step;
        }
        else if (c->lichen == 0 && !c->ice && !c->ore && !c->factory) {
            // Check for growth constraints i.e. pushing up against lichen/factories
            int max_adj_lichen = 0;
            for (Cell *neighbor : c->neighbors) {
                if (neighbor->lichen_strain != -1 && neighbor->lichen_strain != this->id) {
                    if (this->player == board.player
                        && board.opp->is_strain(neighbor->lichen_strain)) {
                        neighbor->lichen_opp_boundary_step = board.step;
                    }
                    return false;
                }
                if (neighbor->factory && neighbor->factory->id != this->id) return false;
                if (neighbor->lichen_strain == this->id && neighbor->lichen > max_adj_lichen) {
                    max_adj_lichen = neighbor->lichen;
                }
            }

            int factory_dist = c->man_dist_factory(this);
            if (c->rubble) {
                if (factory_dist == 1 || max_adj_lichen > 0) {
                    this->lichen_rubble_boundary_cells.push_back(c);
                }
            } else {  // No rubble
                if (factory_dist == 1 || max_adj_lichen >= MIN_LICHEN_TO_SPREAD) {
                    this->lichen_growth_cells.push_back(c);
                }
                if (factory_dist == 1 || max_adj_lichen > 0) {
                    this->lichen_flat_boundary_cells.push_back(c);
                }
                if (max_adj_lichen > 0) {
                    // Neighbor(s) may be frontier
                    for (Cell *neighbor : c->neighbors) {
                        if (neighbor->lichen_strain == this->id) {
                            this->lichen_frontier_cells.push_back(c);  // NOTE: can double-count
                            c->lichen_frontier_step = board.step;
                        }
                    }
                }
            }
        }

        return false;
    };

    board.flood_fill(this->cell, cell_cond);
    if (is_begin_step) {
        this->lichen_connected_count = (int)this->lichen_connected_cells.size();
    }
}

void Factory::update_lichen_bottleneck_info() {
    this->lichen_bottleneck_cells.clear();

    // No lichen, nothing to do
    if (this->lichen_connected_count == 0) return;

    (void)board.pathfind(NULL, NULL, NULL,
                         [&](Cell *c) { (void)c; return false; },
                         [&](Cell *c) { return c->lichen_strain != this->id; },
                         [&](Cell *c, Unit *u) { (void)c; (void)u; return 1; },
                         NULL, INT_MAX,
                         &this->cells);

    for (Cell *cell : this->cells) cell->lichen_dist = 0;
    for (Cell *cell : this->lichen_connected_cells) cell->lichen_dist = cell->path_info.dist;

    for (Cell *cell : this->lichen_connected_cells) {
        if (cell->lichen_dist > 10) continue;
        vector<Cell*> outer_cells;
        for (Cell *neighbor : cell->neighbors) {
            if (neighbor->lichen_strain == this->id && neighbor->lichen_dist > cell->lichen_dist) {
                outer_cells.push_back(neighbor);
            }
        }
        int cell_count = 0;
        int lichen_count = 0;
        for (Cell *outer_cell : outer_cells) {
            int cost = board.pathfind(NULL, outer_cell, NULL,
                                      [&](Cell *c) { return ((c->lichen_strain == this->id
                                                              || (c->factory == this))
                                                             && c->lichen_dist < cell->lichen_dist); },
                                      [&](Cell *c) { return (c == cell
                                                             || c->lichen_strain != this->id); });
            if (cost == INT_MAX) {
                auto cell_cond = [&](Cell *c) {
                    if (c->lichen_strain == this->id && c != cell) {
                        cell_count += 1;
                        lichen_count += c->lichen;
                        return true;
                    } return false;
                };
                board.flood_fill(outer_cell, cell_cond);
            }
        }
        if (cell_count) {
            this->lichen_bottleneck_cells.push_back(cell);
            cell->lichen_bottleneck_step = board.step;
            cell->lichen_bottleneck_cell_count = cell_count;
            cell->lichen_bottleneck_lichen_count = lichen_count;
            //if (board.step % 100 == 0)
            //    LUX_LOG("Bottleneck! " << *this << ' ' << *cell << ' '
            //            << cell_count << ' ' << lichen_count);
        }
    }
}

void Factory::update_lowland_routes(int max_dist, int min_lowland_size) {
    for (auto route : this->lowland_routes) delete route;
    this->lowland_routes.clear();

    int magic_number = -(10 + this->id);
    set<int> checked = { -1 };
    Cell *rc = this->radius_cell(max_dist);
    while (rc) {
        // TODO: Filter out some lowland route destinations? (can still get routes to that region)
        // If rc has non-factory lichen, a route to this cell will not help expand this f's lichen
        // Similary, if rc is (much?) closer to another factory it may not be helpful to dig this way

        if (rc->flatland_size >= min_lowland_size || rc->lowland_size >= min_lowland_size) {  // big
            bool new_route = false;
            if (!checked.count(rc->flatland_id) || !checked.count(rc->lowland_id)) {  // new
                checked.insert(rc->flatland_id);
                checked.insert(rc->lowland_id);
                new_route = true;
            } else {  // seen
                // How near to a processed cell of same region are we (via that region)?
                // Check neighbors first to try to determine quickly
                bool neighbor_processed = false;
                for (Cell *neighbor : rc->neighbors) {
                    if (neighbor->flood_fill_call_id == magic_number) {
                        neighbor_processed = true;
                        break;
                    }
                }
                if (!neighbor_processed) {
                    int dist = board.pathfind(
                        rc, NULL,
                        [&](Cell *c) { return c->flood_fill_call_id == magic_number; },
                        [&](Cell *c) {
                            return !((rc->flatland_id != -1 && rc->flatland_id == c->flatland_id)
                                     || (rc->lowland_id != -1 && rc->lowland_id == c->lowland_id)); },
                        [&](Cell *c, Unit *u) { (void)c; (void)u; return 1; });
                    LUX_ASSERT(dist != INT_MAX);
                    new_route = (dist >= max_dist);
                }
            }

            rc->flood_fill_call_id = magic_number;  // hack
            if (new_route) {
                vector<Cell*> *route = new vector<Cell*>();
                int cost = board.pathfind(
                    this->cell, rc, NULL,
                    [&](Cell *c) { return (c->factory
                                           || c->ore
                                           || c->ice
                                           || c->away_dist <= 2); },
                    [&](Cell *c, Unit *u) {(void)u; return 150 + c->rubble - 5*MIN(10, c->away_dist);},
                    route);
                if (cost != INT_MAX) {
                    this->lowland_routes.push_back(route);
                } else {
                    delete route;
                }
            }
        }
        rc = this->radius_cell(max_dist, rc);
    }
}

void Factory::update_resource_routes(Resource resource, int max_dist, int max_count) {
    // re-sort resource cells based on current opp factory dists
    if (board.real_env_step == 0) {
        stable_sort(this->ice_cells.begin(), this->ice_cells.end(),
                    [&](const Cell *a, const Cell *b) {
                        int adist = a->man_dist_factory(this);
                        int bdist = b->man_dist_factory(this);
                        if (adist == bdist) return a->away_dist > b->away_dist;
                        return adist < bdist; });
        stable_sort(this->ore_cells.begin(), this->ore_cells.end(),
                    [&](const Cell *a, const Cell *b) {
                        int adist = a->man_dist_factory(this);
                        int bdist = b->man_dist_factory(this);
                        if (adist == bdist) return a->away_dist > b->away_dist;
                        return adist < bdist; });
    }

    vector<vector<Cell*>*> &routes = (resource == Resource_ICE
                                      ? this->ice_routes
                                      : this->ore_routes);
    vector<Cell*> &resource_cells = (resource == Resource_ICE
                                     ? this->ice_cells
                                     : this->ore_cells);

    for (auto route : routes) delete route;
    routes.clear();

    for (Cell *resource_cell : resource_cells) {
        int dist = resource_cell->man_dist_factory(this);
        if (dist > max_dist) break;

        vector<Cell*> *route = new vector<Cell*>();
        int cost = board.pathfind(
            this->cell, resource_cell, NULL,
            [&](Cell *c) { return (c->factory
                                   || c->ore
                                   || c->ice
                                   || c->away_dist <= 1); },
            [&](Cell *c, Unit *u) { (void)u; return 100 + c->rubble; },
            route);
        if (cost != INT_MAX && (int)route->size() - 1 <= max_dist) {
            routes.push_back(route);
            if ((int)routes.size() >= max_count) break;
        } else {
            delete route;
        }
    }
}

void Factory::update_units() {  // called at beginning of sim0
    this->units.clear();
    this->heavies.clear();
    this->lights.clear();

    if (this->player == board.player) {
        // Use assigned_factory for my units. Then call add/remove_unit if necessary after sim steps
        for (Unit *unit : this->player->units()) {
            if (unit->assigned_factory == this) {
                this->units.push_back(unit);
                if (unit->heavy) this->heavies.push_back(unit);
                else this->lights.push_back(unit);
            }
        }
    } else {
        // Use last_factory for other units. This will not be updated until next step
        for (Unit *unit : this->player->units()) {
            if (unit->last_factory == this) {
                this->units.push_back(unit);
                if (unit->heavy) this->heavies.push_back(unit);
                else this->lights.push_back(unit);
            }
        }
    }
}

void Factory::add_unit(Unit *unit) {
    LUX_ASSERT(unit->player == board.player);
    this->units.push_back(unit);
    if (unit->heavy) this->heavies.push_back(unit);
    else this->lights.push_back(unit);
}

void Factory::remove_unit(Unit *unit) {
    LUX_ASSERT(unit->player == board.player);
    this->units.remove(unit);
    if (unit->heavy) this->heavies.remove(unit);
    else this->lights.remove(unit);
}

int Factory::get_similar_unit_count(Unit *unit, function<bool(Role*)> const& similar_cond) {
    int count = 0;
    list<Unit*> &_units = (unit->heavy) ? this->heavies : this->lights;
    for (Unit *other_unit : _units) {
        if (other_unit != unit
            && similar_cond(other_unit->role)) count += 1;
    }
    return count;
}

void _create_new_unit(Factory *factory, bool heavy) {
    int unit_id = board.units.size();
    if (board.units.capacity() <= (size_t)unit_id) {
        LUX_ASSERT(false);
    }
    board.units.resize(unit_id + 1);
    board.units[unit_id].init(
        unit_id,
        factory->player->id,
        factory->x,
        factory->y,
        heavy,
        board.sim_step + 1,
        0, 0, 0, 0,
        heavy ? g_heavy_cfg.POWER_COST : g_light_cfg.POWER_COST,
        NULL);
}

bool Factory::_can_build_safely() {
    // Unit here next turn: not safe
    if (this->cell->unit_next) return false;

    // Unit here now, no move planned, insufficient power: not safe
    Unit *unit = this->cell->unit;
    if (unit
        && !unit->cell_next()
        && unit->power < unit->cfg->ACTION_QUEUE_POWER_COST + unit->cfg->MOVE_COST) return false;

    // Too crowded
    if (this->cell->unit
        && this->cell->is_surrounded()) return false;

    return true;
}

bool Factory::can_build_light() {
    return (this->power + this->power_delta >= g_light_cfg.POWER_COST
            && this->metal + this->metal_delta >= g_light_cfg.METAL_COST
            && this->_can_build_safely());
}

void Factory::do_build_light() {
    //if (board.sim0() && this->id == 2) LUX_LOG(*this << " build light");
    this->action = FactoryAction_BUILD_LIGHT;
    this->power_delta -= g_light_cfg.POWER_COST;
    this->metal_delta -= g_light_cfg.METAL_COST;
    _create_new_unit(this, false);
}

bool Factory::can_build_heavy() {
    return (this->power + this->power_delta >= g_heavy_cfg.POWER_COST
            && this->metal + this->metal_delta >= g_heavy_cfg.METAL_COST
            && this->_can_build_safely());
}

void Factory::do_build_heavy() {
    this->action = FactoryAction_BUILD_HEAVY;
    this->power_delta -= g_heavy_cfg.POWER_COST;
    this->metal_delta -= g_heavy_cfg.METAL_COST;
    _create_new_unit(this, true);
}

int Factory::water_cost() {
    return ((this->lichen_growth_cells.size() + LICHEN_WATERING_COST_FACTOR - 1)
            / LICHEN_WATERING_COST_FACTOR);
}

bool Factory::can_water() {
    return this->water >= 1 + this->water_cost();
}

void Factory::do_water() {
    for (Cell *_cell : this->lichen_growth_cells) {
        _cell->lichen += LICHEN_GAINED_WITH_WATER + LICHEN_LOST_WITHOUT_WATER;
        _cell->lichen_strain = this->id;
    }
    this->water_delta -= this->water_cost();
    this->action = FactoryAction_WATER;
}

void Factory::do_none() {
    this->action = FactoryAction_NONE;
}

ostream& operator<<(ostream &os, const struct Factory &f) {
    if (f.player == &board.home.player) os << "F." << f.id;
    else                                os << "F!" << f.id;
    return os;
}
