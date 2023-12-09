#include "lux/board.hpp"

#include <algorithm>  // reverse, stable_sort
#include <queue>  // priority_queue
#include <stack>

#include "agent.hpp"
#include "lux/cell.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/factory.hpp"
#include "lux/log.hpp"
#include "lux/mode.hpp"
#include "lux/mode_default.hpp"
#include "lux/mode_ice_conflict.hpp"
#include "lux/player.hpp"
#include "lux/role.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_chain_transporter.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_recharge.hpp"
#include "lux/team.hpp"
#include "lux/unit.hpp"
using namespace std;


void Board::init(json &obs, int agent_step, bool is_player0) {
    json &board_info = obs.at("board");

    // Init all cells during bidding step
    if (agent_step == 0) {
        this->_flood_fill_call_id = 0;
        this->_pathfind_call_id = 0;
        for (int16_t x = 0; x < SIZE; x++) {
	    for (int16_t y = 0; y < SIZE; y++) {
		int16_t cell_id = y * SIZE + x;
		int ice = board_info.at("ice").at(x).at(y);
		int ore = board_info.at("ore").at(x).at(y);
		int rubble = board_info.at("rubble").at(x).at(y);
		this->cells[cell_id].init(cell_id, x, y, ice, ore, rubble);
	    }
	}
	for (Cell &cell : this->cells) {
	    cell.init_neighbors();
            if (cell.ice) this->ice_cells.push_back(&cell);
            if (cell.ore) this->ore_cells.push_back(&cell);
	}
        for (Cell *ice_cell : this->ice_cells) {
            Cell *rc = ice_cell->radius_cell_factory(1);
            while (rc) {
                rc->ice1_spawn = true;
                rc = ice_cell->radius_cell_factory(1, rc);
            }
	}
        this->factories.reserve(20);
        this->units.reserve(5000); // This must be SAFELY high enough
        //LUX_LOG("cell: " << sizeof(Cell));
        //LUX_LOG("unit: " << sizeof(Unit));
        //LUX_LOG("factory: " << sizeof(Factory));
        //LUX_LOG("player: " << sizeof(Player));
        //LUX_LOG("team: " << sizeof(Team));
        //LUX_LOG("board: " << sizeof(Board));
        this->update_icelands();
    }

    this->real_env_step = obs.at("real_env_steps");
    if (this->real_env_step >= 0) {
	this->step = this->real_env_step;
    } else {
	this->step = agent_step;
    }
    this->sim_step = this->step;

    // Full (re-)init for bidding, factory placement, and first real step
    if (real_env_step <= 0) {
	// Get player water/metal/strains data for each player
	if (agent_step > 0) {  // Not included for bidding step
	    string player_id_str = is_player0 ? "player_0" : "player_1";
	    this->home.init(
		is_player0,
		obs.at("teams").at(player_id_str).at("factory_strains"));
	    string opp_id_str = is_player0 ? "player_1" : "player_0";
	    this->away.init(
		!is_player0,
		obs.at("teams").at(opp_id_str).at("factory_strains"));
            this->player = &this->home.player;
            this->opp = &this->away.player;
	}

	// Get basic factory info
	for (auto &[_, factories_info] : obs.at("factories").items()) {
	    for (auto &[__, factory_info] : factories_info.items()) {
		int16_t factory_id = factory_info.at("strain_id");
		json &cargo_info = factory_info.at("cargo");
                if (this->factories.size() <= (size_t)factory_id) {
                    this->factories.resize(factory_id + 1);
                }
		this->factories[factory_id].init(
		    factory_id,
		    factory_info.at("team_id"),
		    factory_info.at("pos").at(0),
		    factory_info.at("pos").at(1),
		    cargo_info.at("water"),
		    cargo_info.at("metal"));
	    }
	}

        // Update dist from each cell to nearest factory of each team
        for (Cell &cell : this->cells) {
            cell.update_factory_dists();
        }
    } else {  // real_env_step > 0
	// Update cell rubble, lichen, lichen_strain
	for (const auto &[k, v] : board_info.at("rubble").items()) {
	    size_t offset = k.find_first_of(',');
	    int16_t x = static_cast<int16_t>(std::stol(k.substr(0, offset)));
	    int16_t y = static_cast<int16_t>(std::stol(k.substr(offset + 1)));
	    this->cell(x, y)->reinit_rubble(v);
	}  for (const auto &[k, v] : board_info.at("lichen").items()) {
	    size_t offset = k.find_first_of(',');
	    int16_t x = static_cast<int16_t>(std::stol(k.substr(0, offset)));
	    int16_t y = static_cast<int16_t>(std::stol(k.substr(offset + 1)));
	    this->cell(x, y)->reinit_lichen(v);
	}  for (const auto &[k, v] : board_info.at("lichen_strains").items()) {
	    size_t offset = k.find_first_of(',');
	    int16_t x = static_cast<int16_t>(std::stol(k.substr(0, offset)));
	    int16_t y = static_cast<int16_t>(std::stol(k.substr(offset + 1)));
	    this->cell(x, y)->reinit_lichen_strain(v);
	}

        // init/re-init units
        for (auto &[_, units_info] : obs.at("units").items()) {
            for (auto &[__, unit_info] : units_info.items()) {
                string unit_id_str = unit_info.at("unit_id");
                int16_t unit_id = stoi(unit_id_str.substr(unit_id_str.find("_") + 1));
                json &cargo_info = unit_info.at("cargo");

                if (this->units.capacity() <= (size_t)unit_id) {
                    LUX_ASSERT(false);
                }
                if (this->units.size() <= (size_t)unit_id) {
                    this->units.resize(unit_id + 1);
                }

                string unit_type = unit_info.at("unit_type");
                this->units[unit_id].init(
                    unit_id,
                    unit_info.at("team_id"),
                    unit_info.at("pos").at(0),
                    unit_info.at("pos").at(1),
                    unit_type.at(0) == 'H',
                    this->step,
                    cargo_info.at("ice"),
                    cargo_info.at("ore"),
                    cargo_info.at("water"),
                    cargo_info.at("metal"),
                    unit_info.at("power"),
                    &unit_info.at("action_queue"));  // json array of json arrays
            }
        }

	// Get non-basic factory info
	for (auto &[_, factories_info] : obs.at("factories").items()) {
	    for (auto &[__, factory_info] : factories_info.items()) {
		int16_t factory_id = factory_info.at("strain_id");
		json &cargo_info = factory_info.at("cargo");
		this->factories[factory_id].reinit(
		    cargo_info.at("ice"),
		    cargo_info.at("ore"),
		    cargo_info.at("water"),
		    cargo_info.at("metal"),
		    factory_info.at("power"));
	    }
	}

	// Check for newly destroyed units and factories
        for (Unit &unit : this->units) {
            if (unit.alive_step && unit.alive_step == this->step - 1) {
                unit.handle_destruction();
            }
        }
        for (Factory &factory : this->factories) {
	    if (factory.alive_step && factory.alive_step == this->step - 1) {
		factory.handle_destruction();

                // Update dist from each cell to nearest factory of each team
                for (Cell &cell : this->cells) {
                    cell.update_factory_dists();
                }
	    }
	}
    }

    if (board_info.find("valid_spawns_mask") != board_info.end()) {
	for (int16_t x = 0; x < SIZE; x++) {
	    for (int16_t y = 0; y < SIZE; y++) {
		this->cell(x, y)->valid_spawn = board_info.at("valid_spawns_mask").at(x).at(y);
	    }
	}
    }
}

string Board::summary() {
    int pf = this->home.player.factories().size();
    int of = this->away.player.factories().size();

    int phu = 0;
    for (Unit *u : this->home.player.units()) if (u->heavy) phu++;
    int plu = this->home.player.units().size() - phu;

    int ohu = 0;
    for (Unit *u : this->away.player.units()) if (u->heavy) ohu++;
    int olu = this->away.player.units().size() - ohu;

    int pp = 0;
    for (Unit *u : this->home.player.units()) pp += u->power;
    for (Factory *f : this->home.player.factories()) pp += f->power;
    pp /= 1000;

    int op = 0;
    for (Unit *u : this->away.player.units()) op += u->power;
    for (Factory *f : this->away.player.factories()) op += f->power;
    op /= 1000;

    int plt = 0, olt = 0, plc = 0, olc = 0;
    for (Cell &c : this->cells) {
        if (c.lichen > 0) {
            if (this->player->is_strain(c.lichen_strain)) { plt += c.lichen; plc++; }
            else { olt += c.lichen; olc++; }
        }
    }

    double pi = 50 * pf + 6 * phu + 0.6 * plu + 1 * plc;
    double oi = 50 * of + 6 * ohu + 0.6 * olu + 1 * olc;

    int unit_steps = 0;
    int aq_count = 0;
    for (Unit &u : board.units) {
        if (u.player == board.player) {
            unit_steps += (u.alive_step - u.build_step);
            aq_count += u.action_queue_update_count;
        }
    }
    double aqlen = (aq_count > 0 ? unit_steps / (double)aq_count : 0);

    return (
        ""
        + to_string(pf) + "-" + to_string(of) + "F, "
        + to_string(phu) + "-" + to_string(ohu) + "HU, "
        + to_string(plu) + "-" + to_string(olu) + "LU, "
        + to_string(pp) + "-" + to_string(op) + "kP, "
        + to_string(plt) + "-" + to_string(olt) + "L, "
        + to_string((int)pi) + "-" + to_string((int)oi) + "I, "
        + to_string(aqlen) + "/aq");
}

void Board::save_begin() {
    // Save some stats e.g. position for each unit
    this->_save_units_len = this->units.size();
    for (Cell &cell : this->cells) {
	cell.save_begin();
    }
}

void Board::save_end() {
    // Save roles, routes, modes, stats, assignments, etc
    for (Unit *unit : this->player->units()) {
        unit->save_end();
    }
    for (Factory *factory : this->player->factories()) {
        factory->save_end();
    }
    for (Cell &cell : this->cells) {
        cell.save_end();
    }
}

void Board::load() {
    // Load saved roles, routes, modes, stats, assignments, etc
    this->units.resize(this->_save_units_len);
    for (Unit *unit : this->player->units()) {
        unit->load();
    }
    for (Factory *factory : this->player->factories()) {
        factory->load();
    }
    for (Cell &cell : this->cells) {
	cell.load();
    }
}

Cell *Board::cell(int x, int y) {
    if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return NULL;
    return &this->cells[y * SIZE + x];
}

Cell *Board::cell(int cell_id) {
    if (cell_id < 0 || cell_id >= SIZE2) return NULL;
    return &this->cells[cell_id];
}

Player *Board::get_player(int player_id) {
    if (this->home.player.id == player_id) return &this->home.player;
    if (this->away.player.id == player_id) return &this->away.player;
    LUX_ASSERT(false);
    return NULL;
}

void Board::begin_step_simulation() {
    if ((this->step % 100 == 0) && this->sim0()) {  // Only sometimes; TODO: after factories explode?
        this->update_flatlands();
        this->update_lowlands();

        for (Factory *factory : this->player->factories()) {
            factory->update_lowland_routes();
            factory->update_resource_routes(Resource_ICE, /*dist*/10, /*count*/6);
            factory->update_resource_routes(Resource_ORE, /*dist*/25, /*count*/3);
        }
    }

    for (Factory *factory : this->player->factories()) {
        factory->ice_delta = 0;
        factory->ore_delta = 0;
        factory->water_delta = 0;
        factory->metal_delta = 0;
        factory->power_delta = 0;
        factory->update_lichen_info(/*is_begin_step*/true);
        /*if (factory->id == 0) {
            LUX_LOG("lichen " << *factory << ' ' << factory->lichen_connected_cells.size());
            LUX_LOG("power " << *factory << ' ' << factory->power);
        }*/
    }

    for (Unit *unit : this->opp->units()) {
        unit->oscillating_unit = NULL;
    }

    for (Unit *unit : this->player->units()) {
        unit->x_delta = INT16_MAX;
        unit->y_delta = INT16_MAX;
        unit->ice_delta = 0;
        unit->ore_delta = 0;
        unit->water_delta = 0;
        unit->metal_delta = 0;
        unit->power_delta = 0;
        unit->assigned_unit = NULL;
        if (unit->assigned_factory == NULL) {
            // must be before f.update_units
            unit->assigned_factory = unit->cell()->nearest_home_factory;
            unit->assigned_factory->add_unit(unit);
        }
        unit->normalize_action_queue();
    }

    if (this->sim0()) {  // Only once per step
        for (Cell &cell : this->cells) {  // all cells
            cell.update_unit_history(cell.unit);
            cell.future_heavy_dig_step = INT_MAX;
            cell.future_light_dig_step = INT_MAX;
            cell.lichen_dist = INT_MAX;
        }

        for (Unit &unit : this->units) {  // all units
            unit.update_stats_begin();  // must be before f.update_units
        }

        for (Factory &factory : this->factories) {  // all factories
            factory.update_units();  // must be after u.assigned_factory, u.last_factory
            if (factory.player == board.opp) factory.update_lichen_info(/*is_begin_step*/true);
            factory.update_lichen_bottleneck_info();
        }

        this->update_disconnected_lichen();
        this->update_future_mines();  // must be after cell._dig_step updates
        this->update_opp_chains();
    }

    LUX_LOG_DEBUG("BSS A");
    this->update_roles_and_goals();
    LUX_LOG_DEBUG("BSS B");
}

void Board::end_step_simulation() {
    // Add new units to cached player/team/(factory?) unit lists
    // Newly created units need to get power_gain this step (but they don't have roles yet!)
    this->player->add_new_units();

    // Set next step's factory power and resources
    for (Factory *factory : this->player->factories()) {
        factory->ice += factory->ice_delta;
        factory->ore += factory->ore_delta;
        factory->water += factory->water_delta;
        factory->metal += factory->metal_delta;
        factory->power += factory->power_delta;

        factory->power += factory->power_gain();

        int new_water = MIN(FACTORY_PROCESSING_RATE_WATER, factory->ice) / ICE_WATER_RATIO;
        factory->water += new_water;
        factory->water -= FACTORY_WATER_CONSUMPTION;
        factory->ice -= new_water * ICE_WATER_RATIO;

        int new_metal = MIN(FACTORY_PROCESSING_RATE_METAL, factory->ore) / ORE_METAL_RATIO;
        factory->metal += new_metal;
        factory->ore -= new_metal * ORE_METAL_RATIO;
    }

    // TODO: update cell->unit_predict for each non-player unit based on AQ or recent pattern
    //       This could be useful for Unit::move_risk
    for (Unit *unit : this->player->units()) {
        unit->x += unit->x_delta;
        unit->y += unit->y_delta;

        unit->ice += unit->ice_delta;
        unit->ore += unit->ore_delta;
        unit->water += unit->water_delta;
        unit->metal += unit->metal_delta;
        LUX_ASSERT(0 <= unit->ice && unit->ice <= unit->cfg->CARGO_SPACE);
        LUX_ASSERT(0 <= unit->ore && unit->ore <= unit->cfg->CARGO_SPACE);
        LUX_ASSERT(0 <= unit->water && unit->water <= unit->cfg->CARGO_SPACE);
        LUX_ASSERT(0 <= unit->metal && unit->metal <= unit->cfg->CARGO_SPACE);

        unit->power += unit->power_delta;
        unit->power += unit->power_gain();
        unit->power = MAX(MIN(unit->power, unit->cfg->BATTERY_CAPACITY), 0);

        //if (unit->_log_cond()) LUX_LOG("Do " << *unit << ' ' << unit->action);

        // Update unit->factory and cell->factory assignments
        if (unit->build_step <= this->sim_step) {
            // Update factory assignment for each of my units
            unit->update_assigned_factory();

            // Update factory assignment for previously unclaimed ice cells
            if (unit->heavy) {
                RoleMiner *role_miner = RoleMiner::cast(unit->role);
                if (role_miner
                    && role_miner->resource_cell->ice
                    && role_miner->resource_cell->assigned_factory == NULL) {
                    role_miner->resource_cell->assigned_factory = role_miner->get_factory();
                }
            }

            // Always clean up at end of sim_step by calling role.unset
            unit->role->unset();
        }
    }

    // Assume opp lichen will grow, decrement everything else.
    for (Cell &cell : this->cells) {
        if (this->player->is_strain(cell.lichen_strain)) {
            cell.lichen -= LICHEN_LOST_WITHOUT_WATER;
        } else if (cell.lichen > 0) {
            // TODO: instead of this growing up-but-not-out logic, we could just have simple logic
            //       for calling factory.do_water for each opp factory each step (e.g. over 100 water).
            //       We would have to call update_lichen_info for opp factories (maybe via can_water())
            //       and would have to apply water_delta to opp factory's water
            cell.lichen += LICHEN_GAINED_WITH_WATER;
        }
        cell.lichen = MAX(MIN(cell.lichen, MAX_LICHEN_PER_TILE), 0);
        if (cell.lichen == 0) cell.lichen_strain = -1;

        cell.unit = cell.unit_next;
        cell.unit_next = NULL;
    }
}

void Board::update_roles_and_goals() {
    // Validate existing factory modes
    LUX_LOG_DEBUG("URG A");
    for (Factory *factory : this->player->factories()) {
        if (factory->mode) {
            if (factory->mode->is_valid()) factory->mode->set();
            else factory->delete_mode();
        }
    }

    // Check for special factory mode-changing criteria
    LUX_LOG_DEBUG("URG B1");
    for (Factory *factory : this->player->factories()) {
        Mode *m = NULL;
        bool success = (
            false
            || ModeIceConflict::from_transition_antagonized(&m, factory)
            );
        LUX_ASSERT(!success == !m);
        if (m) factory->new_mode(m);
    }

    // Set modes for factories without one
    LUX_LOG_DEBUG("URG B2");
    for (Factory *factory : this->player->factories()) {
        if (factory->mode) continue;
        Mode *m = NULL;
        bool success = (
            false
            || ModeIceConflict::from_ice_superiority(&m, factory)
            || ModeIceConflict::from_desperation(&m, factory)
            || ModeDefault::from_factory(&m, factory)
            );
        LUX_ASSERT(success);
        LUX_ASSERT(m);
        factory->new_mode(m);
    }

    // Validate existing roles
    LUX_LOG_DEBUG("URG C");
    for (Unit *unit : this->player->units()) {
        if (unit->role) {
            if (unit->role->is_valid()) unit->role->set();
            else unit->delete_role();
        }
        unit->update_low_power();
    }

    // Check for special role-changing criteria
    LUX_LOG_DEBUG("URG D1");
    if (this->sim0()) RolePincer::transition_units();

    // Check for special role-changing criteria
    LUX_LOG_DEBUG("URG D2");
    for (Unit *unit : this->player->units()) {
        LUX_LOG_DEBUG("URG D2 " << *unit);
        Role *r = unit->assigned_factory->mode->get_transition_role(unit);
        if (r) unit->new_role(r);
        if (r) LUX_LOG_DEBUG("URG D2 " << *unit << ' ' << *r);
    }

    // Set roles for units without one, continue until all units have roles
    LUX_LOG_DEBUG("URG E");
    for (int loop_count = 0, new_role_count = -1; new_role_count != 0; loop_count++) {
        LUX_LOG_DEBUG("URG E " << loop_count << ' ' << new_role_count);
        LUX_ASSERT(loop_count < 50);
        new_role_count = 0;

        for (Unit *unit : this->player->units()) {
            if (unit->role) continue;
            LUX_LOG_DEBUG("URG E1 " << *unit);
            Role *r = unit->assigned_factory->mode->get_new_role(unit);
            unit->new_role(r);
            LUX_LOG_DEBUG("URG E2 " << *unit << ' ' << *r);
            new_role_count += 1;
        }
    }

    // Update goals
    LUX_LOG_DEBUG("URG F");
    for (Unit *unit : this->player->units()) {
        unit->update_goal();
    }
}

void Board::update_flatlands() {
    for (Cell &cell : this->cells) {
        cell.flatland_id = -1; cell.flatland_size = -1;
    }

    vector<Cell*> flatland_cells;
    auto cell_cond = [&](Cell *c) {
        if (c->rubble <= 0 && !c->factory && !c->ore && !c->ice) {
            flatland_cells.push_back(c);
            return true;
        } return false;
    };

    int16_t next_id = 1;
    for (Cell &cell : this->cells) {
        if (cell.flatland_id == -1 && cell_cond(&cell)) {
            flatland_cells.clear();
            this->flood_fill(&cell, cell_cond);
            for (Cell *fcell : flatland_cells) {
                fcell->flatland_id = next_id;
                fcell->flatland_size = flatland_cells.size();
            }
            next_id += 1;
            //LUX_LOG("Board::update_flatlands " << cell << ' '
            //        << cell.flatland_id << ' ' << cell.flatland_size);
        }
    }
}

void Board::update_lowlands() {
    for (Cell &cell : this->cells) {
        cell.lowland_id = -1; cell.lowland_size = -1;
    }

    vector<Cell*> lowland_cells;
    auto cell_cond = [&](Cell *c) {
        if (c->rubble <= 19 && !c->factory) {  // Allow own factories?
            lowland_cells.push_back(c);
            return true;
        } return false;
    };

    int16_t next_id = 10000;
    for (Cell &cell : this->cells) {
        if (cell.lowland_id == -1 && cell_cond(&cell)) {
            lowland_cells.clear();
            this->flood_fill(&cell, cell_cond);
            for (Cell *lcell : lowland_cells) {
                lcell->lowland_id = next_id;
                lcell->lowland_size = lowland_cells.size();
            }
            next_id += 1;
            //LUX_LOG("Board::update_lowlands " << cell << ' '
            //        << cell.lowland_id << ' ' << cell.lowland_size);
        }
    }
}

bool Board::low_iceland() {
    // 109: 5, 20
    // 44570609: 4, 20
    // 263344553: 10, 20
    // 517525016: 8, 8
    // 682050041: 6, 14
    // 917281543: 3, 12
    return 2 * this->iceland_count <= 2*agent.factories_per_team;
}

void Board::update_icelands() {
    this->iceland_count = 0;
    for (Cell &cell : this->cells) {
        cell.iceland_id = -1; cell.iceland_size = -1;
    }

    vector<Cell*> iceland_cells;
    auto cell_cond = [&](Cell *c) {
        //for (Cell *neighbor : c->neighbors_plus) {
        //    if (neighbor->ice) {
        //        iceland_cells.push_back(c);
        //        return true;
        //    }
        //}
        //return false;

        bool has_ice = c->ice;
        if (!has_ice) {
            int ice_count = 0;
            for (Cell *neighbor : c->neighbors) {
                if (neighbor->ice) ice_count++;
            }
            if (ice_count >= 2) has_ice = true;
        }
        if (has_ice) {
            iceland_cells.push_back(c);
            return true;
        }
        return false;
    };

    int16_t next_id = 1;
    for (Cell &cell : this->cells) {
        if (cell.iceland_id == -1 && cell_cond(&cell)) {
            iceland_cells.clear();
            this->flood_fill(&cell, cell_cond);
            for (Cell *fcell : iceland_cells) {
                fcell->iceland_id = next_id;
                fcell->iceland_size = iceland_cells.size();
            }
            next_id += 1;
            this->iceland_count += 1 + iceland_cells.size() / 9;
            LUX_LOG("Board::update_icelands " << cell << ' '
                    << cell.iceland_id << ' ' << cell.iceland_size);
        }
    }

    LUX_LOG("Ice regions: " << this->iceland_count << ' ' << 2*agent.factories_per_team);
}

void Board::update_disconnected_lichen() {
    this->player->lichen_disconnected_cells.clear();
    this->opp->lichen_disconnected_cells.clear();

    for (Cell &cell : this->cells) {  // all cells
        if (cell.lichen && cell.lichen_connected_step != this->step) {
            Player *player = (this->player->is_strain(cell.lichen_strain) ? this->player : this->opp);
            player->lichen_disconnected_cells.push_back(&cell);
        }
    }
}

void Board::update_future_mines() {
    LUX_ASSERT(board.step == board.sim_step);

    this->future_heavy_mine_cell_steps.clear();
    this->future_light_mine_cell_steps.clear();
    this->future_heavy_cow_cell_steps.clear();
    this->future_light_cow_cell_steps.clear();

    for (Unit *opp_unit : this->opp->units()) {
        opp_unit->future_mine_cell_steps.clear();
        auto &mine_cell_steps = (opp_unit->heavy
                                 ? this->future_heavy_mine_cell_steps
                                 : this->future_light_mine_cell_steps);
        auto &cow_cell_steps = (opp_unit->heavy
                                ? this->future_heavy_cow_cell_steps
                                : this->future_light_cow_cell_steps);
        Cell *cell = opp_unit->cell();
        for (int i = 0; i < opp_unit->aq_len; i++) {
            if (!cell) break;
            int s = board.step + i;
            ActionSpec &spec = opp_unit->action_queue[i];
            if (spec.action == UnitAction_MOVE) {
                cell = cell->neighbor(spec.direction);
            } else if (spec.action == UnitAction_DIG) {
                if (cell->ice || cell->ore) {
                    mine_cell_steps.push_back(make_pair(cell, s));
                    opp_unit->future_mine_cell_steps.push_back(make_pair(cell, s));
                } else if (cell->rubble) {
                    cow_cell_steps.push_back(make_pair(cell, s));
                }
                if (opp_unit->heavy) cell->future_heavy_dig_step = MIN(cell->future_heavy_dig_step, s);
                else cell->future_light_dig_step = MIN(cell->future_light_dig_step, s);
            }
        }
    }

    stable_sort(this->future_heavy_mine_cell_steps.begin(), this->future_heavy_mine_cell_steps.end(),
                [&](const pair<Cell*,int> &a, const pair<Cell*,int> &b) {
                    return a.second < b.second; });
    stable_sort(this->future_light_mine_cell_steps.begin(), this->future_light_mine_cell_steps.end(),
                [&](const pair<Cell*,int> &a, const pair<Cell*,int> &b) {
                    return a.second < b.second; });
    stable_sort(this->future_heavy_cow_cell_steps.begin(), this->future_heavy_cow_cell_steps.end(),
                [&](const pair<Cell*,int> &a, const pair<Cell*,int> &b) {
                    return a.second < b.second; });
    stable_sort(this->future_light_cow_cell_steps.begin(), this->future_light_cow_cell_steps.end(),
                [&](const pair<Cell*,int> &a, const pair<Cell*,int> &b) {
                    return a.second < b.second; });
}

void Board::update_opp_chains() {
    // validate existing known chains
    vector<Unit*> invalid_chain_units;
    for (auto &opp_chain : this->opp_chains) {
        bool is_valid = true;

        Unit *opp_unit = opp_chain.first;
        vector<Cell*> *chain_route = opp_chain.second;
        Cell *resource_cell = chain_route->back();

        if (opp_unit->cell()->man_dist(resource_cell) > 1) is_valid = false;
        if (is_valid) {
            // TODO: other checks?
        }

        if (!is_valid) {
            invalid_chain_units.push_back(opp_unit);
            delete chain_route;
        }
    }
    for (Unit *opp_unit : invalid_chain_units) {
        this->opp_chains.erase(opp_unit);
    }

    // find new chains, and update existing chains if they've changed
    vector<Cell*> chain_route;
    for (Factory *opp_factory : this->opp->factories()) {
        int cost = this->pathfind(
            opp_factory->cell, NULL,
            [&](Cell *c) {
                return ((c->ice || c->ore)
                        && c->man_dist_factory(opp_factory) > 1
                        && c->opp_unit()
                        && c->opp_unit()->heavy); },  // TODO: also check past/future digs?
            [&](Cell *c) {
                Unit *u = c->opp_unit();
                return !(u
                         && !u->heavy
                         && !c->factory
                         && u->is_chain()); },
            [&](Cell *c, Unit *u) { (void)c; (void)u; return 1; },
            &chain_route);
        if (cost != INT_MAX) {
            Unit *miner_unit = chain_route.back()->opp_unit();
            LUX_ASSERT(miner_unit && miner_unit->heavy);

            // if unit already in map, delete the route
            auto it = this->opp_chains.find(miner_unit);
            if (it != this->opp_chains.end()) {
                delete it->second;
            }

            // set new/updated chain route for opp miner unit
            this->opp_chains[miner_unit] = new vector<Cell*>(chain_route);

            //LUX_LOG("chain " << *miner_unit << ' ' << *chain_route.back() << ' '
            //        << chain_route.back()->man_dist(chain_route.front()));
        }
    }
}

void Board::flood_fill(Cell *src, function<bool(Cell*)> const& cell_cond) {
    this->_flood_fill_call_id += 1;
    stack<Cell*> stack;
    stack.push(src); src->flood_fill_call_id = this->_flood_fill_call_id;
    while (!stack.empty()) {
        Cell *cell = stack.top(); stack.pop();
        if (cell_cond(cell)) {
            for (Cell *neighbor : cell->neighbors) {
                if (neighbor->flood_fill_call_id != this->_flood_fill_call_id) {
                    stack.push(neighbor); neighbor->flood_fill_call_id = this->_flood_fill_call_id;
                }
            }
        }
    }
}

int _naive_cost_around_factory(Unit *unit, Factory *factory, Cell *src, Cell *dest, bool clockwise) {
    int cost = 0;
    Cell *cell = src;
    while (cell && cell != dest) {  // cell may exit map
        if (cell->man_dist_factory(factory) == 2) {  // corner
            if (cell->x > factory->x && cell->y > factory->y)  // southeast
                cell = clockwise ? cell->west : cell->north;
            else if (cell->x > factory->x && cell->y < factory->y)  // northeast
                cell = clockwise ? cell->south : cell->west;
            else if (cell->x < factory->x && cell->y > factory->y)  // southwest
                cell = clockwise ? cell->north : cell->east;
            else  // northwest
                cell = clockwise ? cell->east : cell->south;
        } else {  // edge
            if (cell->x + 2 == factory->x)  // west
                cell = clockwise ? cell->north : cell->south;
            else if (cell->x - 2 == factory->x)  // east
                cell = clockwise ? cell->south : cell->north;
            else if (cell->y + 2 == factory->y)  // north
                cell = clockwise ? cell->east : cell->west;
            else  // south
                cell = clockwise ? cell->west : cell->east;
        }
        if (cell) cost += unit->move_basic_cost(cell);
    }
    return cell ? cost : INT_MAX;
}

int _naive_cost_around_factory(Unit *unit, Factory *factory, Cell *src, Cell *dest) {
    int cost_c = _naive_cost_around_factory(unit, factory, src, dest, true);
    int cost_cc = _naive_cost_around_factory(unit, factory, src, dest, false);
    return MIN(cost_c, cost_cc);
}

int Board::naive_cost(Unit *unit, Cell *src, Cell *dest_cell) {
    if (!unit || !src || !dest_cell) {
        LUX_LOG("WARNING: bad naive_cost args " << *unit << ' ' << *src << ' ' << *dest_cell);
        if (unit && unit->role) LUX_LOG("  role: " << *unit->role);
    }
    if (src->opp_factory(unit->player)) {
        LUX_LOG("WARNING: bad naive_cost call " << *unit << ' ' << *src << ' ' << *dest_cell);
        if (unit->role) LUX_LOG("  role: " << *unit->role);
        return unit->cfg->MOVE_COST * src->man_dist(dest_cell);
    }

    LUX_ASSERT(unit);
    LUX_ASSERT(src);
    LUX_ASSERT(dest_cell);

    int cost = 0;
    Cell *prev_cell = NULL;
    Cell *cell = src;
    int cur_dist = src->man_dist(dest_cell);
    Factory *opp_factory = NULL;
    Cell *opp_factory_prev_cell = NULL;

    while ((cell != dest_cell)
           && !(dest_cell->factory_center && cell->factory == dest_cell->factory)) {
        // Entered opp factory
        if (!opp_factory && cell->opp_factory(unit->player)) {
            LUX_ASSERT(prev_cell);  // should not be called from opp factory
            opp_factory = cell->factory;
            opp_factory_prev_cell = prev_cell;
            cost -= unit->cfg->MOVE_COST;
        }

        // Consider moves
        Cell *best_cell = NULL;
        int min_rubble = INT_MAX;
        for (Cell *neighbor : cell->neighbors) {
            if (neighbor->rubble < min_rubble && neighbor->man_dist(dest_cell) < cur_dist) {
                best_cell = neighbor; min_rubble = neighbor->rubble;
            }
        }

        // Update for move
        prev_cell = cell;
        cell = best_cell;
        cur_dist -= 1;
        if (!opp_factory) cost += unit->move_basic_cost(cell);

        // Exited opp factory
        if (opp_factory && !cell->factory) {
            cost += _naive_cost_around_factory(unit, opp_factory, opp_factory_prev_cell, cell);
            opp_factory = NULL;
        }
    }
    return cost;
}

// Note: Always check or assert that return value is not INT_MAX
int Board::pathfind(Unit *unit, Cell *src, Cell *dest_cell,
                    function<bool(Cell*)> const& dest_cond,
                    function<bool(Cell*)> const& avoid_cond,
                    function<int(Cell*,Unit*)> const& custom_cost,
                    vector<Cell*> *route,
                    int max_dist,
                    vector<Cell*> *src_cells) {
    LUX_ASSERT(dest_cell || dest_cond);

    this->_pathfind_call_id += 1;
    int const move_cost = (unit == NULL ? 20 : unit->cfg->MOVE_COST);
    double const rubble_movement_cost = (unit == NULL ? 1 : unit->cfg->RUBBLE_MOVEMENT_COST);
    bool const a_star = (dest_cell && !custom_cost);

    int cost = 0;
    auto cost_pair = make_pair(cost, cost);
    priority_queue<PIII, vector<PIII>, greater<PIII> > queue;
    if (src_cells) {
        LUX_ASSERT(!src);
        LUX_ASSERT(!src_cells->empty());
        for (Cell *scell : *src_cells) {
            cost_pair = make_pair(cost, cost);
            if (a_star) cost_pair.first += move_cost * scell->man_dist(dest_cell);
            scell->path_info = {
                .cost = cost,
                .dist = 0,
                .prev_cell = NULL,
                .call_id = this->_pathfind_call_id};
            queue.push(make_pair(cost_pair, scell->id));  // cost, id
        }
    } else if (!unit && src->factory_center) {
        // If src is a factory_center, start from outer factory cells
        for (Cell *fcell : src->factory->cells) {
            cost_pair = make_pair(cost, cost);
            if (a_star) cost_pair.first += move_cost * fcell->man_dist(dest_cell);
            fcell->path_info = {
                .cost = cost,
                .dist = 0,
                .prev_cell = NULL,
                .call_id = this->_pathfind_call_id};
            queue.push(make_pair(cost_pair, fcell->id));  // cost, id
        }
    } else {
        if (a_star) cost_pair.first += move_cost * src->man_dist(dest_cell);
        src->path_info = {
            .cost = cost,
            .dist = 0,
            .prev_cell = NULL,
            .call_id = this->_pathfind_call_id};
        queue.push(make_pair(cost_pair, src->id));  // cost, id
    }

    while (!queue.empty()) {
	auto top = queue.top();
	queue.pop();
	Cell *cell = this->cell(top.second);
	if (top.first.second > cell->path_info.cost) continue;  // outdated duplicate

	// Check for terminal condition
	if (cell == dest_cell
            || (dest_cond && dest_cond(cell))
            || (!dest_cond  // Implied-factory destination
                && dest_cell->factory_center
                && cell->factory == dest_cell->factory
                && ((unit && unit->player == board.opp)
                    || !cell->assigned_unit
                    || cell->assigned_unit == unit))) {
	    cost = cell->path_info.cost;
	    if (route) {
		route->clear();
		while (cell) {
		    route->push_back(cell);
		    cell = cell->path_info.prev_cell;
		}
		reverse(route->begin(), route->end());
	    }
	    return cost;
	}

        // Check for distance limit
        if (cell->path_info.dist >= max_dist) continue;

        // Check if cell cannot be passed through
        //  - src can always be passed through
        //  - Always avoid opp factory cells
        if (cell->path_info.cost
            && ((avoid_cond && avoid_cond(cell))
                || (unit && cell->opp_factory(unit->player)))) continue;

        // Don't wander through factory when implied-factory destination
        if (cell->path_info.cost && !avoid_cond
            && dest_cell->factory_center && cell->factory == dest_cell->factory) continue;

	// Update neighbors
	for (Cell *new_cell : cell->neighbors) {
	    // Init new_cell if first time this invocation
	    if (new_cell->path_info.call_id != this->_pathfind_call_id) {
		new_cell->path_info = {
		    .cost = INT_MAX,
                    .dist = INT_MAX,
                    .prev_cell = NULL,
                    .call_id = this->_pathfind_call_id};
	    }

            if (custom_cost) cost = cell->path_info.cost + custom_cost(new_cell, unit);
            else cost = (cell->path_info.cost
                         + move_cost
                         + static_cast<int>(rubble_movement_cost * new_cell->rubble));
            int seed = board.sim_step + cell->id + new_cell->id + (unit ? unit->id : 0);
            if (cost < new_cell->path_info.cost) {
                new_cell->path_info.cost = cost;
                new_cell->path_info.dist = cell->path_info.dist + 1;
                new_cell->path_info.prev_cell = cell;
                cost_pair = make_pair(cost, cost);
                if (a_star) cost_pair.first += move_cost * new_cell->man_dist(dest_cell);
                queue.push(make_pair(cost_pair, new_cell->id));
	    } else if (cost == new_cell->path_info.cost
                       && (!unit || !RoleBlockade::cast(unit->role))
                       && prandom(seed, 0.5)) {
                new_cell->path_info.dist = cell->path_info.dist + 1;
                new_cell->path_info.prev_cell = cell;
            }
	}
    }

    return INT_MAX;
}

int Board::pathfind(Cell *src, Cell *dest_cell,
                    function<bool(Cell*)> const& dest_cond,
                    function<bool(Cell*)> const& avoid_cond,
                    function<int(Cell*,Unit*)> const& custom_cost,
                    vector<Cell*> *route,
                    int max_dist) {
    return this->pathfind(NULL, src, dest_cell, dest_cond, avoid_cond, custom_cost, route, max_dist);
}
