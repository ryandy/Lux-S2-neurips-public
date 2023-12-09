#include "lux/mode_ice_conflict.hpp"

#include <map>
#include <string>

#include "lux/board.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/mode_default.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_water_transporter.hpp"
using namespace std;


ModeIceConflict::ModeIceConflict(Factory *_factory, Factory *_opp_factory,
                                 bool _defensive, bool _desperate)
    : Mode(_factory), opp_factory(_opp_factory), defensive(_defensive), desperate(_desperate)
{
    this->offensive = (!_defensive && !_desperate);
}

bool ModeIceConflict::from_transition_antagonized(Mode **new_mode, Factory *_factory) {
    LUX_ASSERT(new_mode);
    LUX_ASSERT(_factory);

    if (!board.sim0()) return false;

    // Skip factories that are already in ice conflict mode
    if (ModeIceConflict::cast(_factory->mode)) return false;

    // Really need a friendly factory to pull this off
    if (board.player->factories().size() == 1) return false;

    // Only applicable for single-heavy factories
    if (_factory->heavies.size() != 1) return false;

    // Check for opp antagonizer unit
    Unit *heavy_unit = _factory->heavies.back();
    Unit *antagonizer_unit = heavy_unit->antagonizer_unit;
    if (!antagonizer_unit) return false;

    // Check for low water
    int factory_water = _factory->total_water(/*extra_ice*/heavy_unit->ice);
    if (factory_water >= 130) return false;

    // Check for heavy ice miner near its resource cell
    RoleMiner *role_miner = RoleMiner::cast(heavy_unit->role);
    if (!role_miner
        || !role_miner->resource_cell->ice
        || heavy_unit->cell()->man_dist(role_miner->resource_cell) >= 2) return false;

    bool is_contested = role_miner->resource_cell->is_contested();
    int future_antagonize_steps = antagonizer_unit->power / antagonizer_unit->cfg->MOVE_COST;
    if (!is_contested
        && future_antagonize_steps < factory_water - 20) {
        LUX_LOG(*heavy_unit << " antagonized but can outlast " << *antagonizer_unit);
        return false;
    }

    // If miner can transition to a safer cell, let them try that first
    Role *unused = NULL;
    bool can_transition = RoleMiner::from_transition_to_uncontested_ice(&unused, heavy_unit);
    if (can_transition) {
        LUX_ASSERT(unused);
        delete unused;
        LUX_LOG(*heavy_unit << " antagonized, wait to transition ice cell before getting defensive");
        return false;
    } else {
        LUX_LOG(*heavy_unit << " antagonized, has no uncontested ice cell to transition to");
    }

    Factory *opp_factory = antagonizer_unit->last_factory;
    LUX_ASSERT(opp_factory);
    if (opp_factory->cell->man_dist_factory(_factory) > 15
        && future_antagonize_steps < factory_water - 20) {
        LUX_LOG(*heavy_unit << " antagonized but can outlast distant " << *antagonizer_unit);
    }

    // Force units to find new jobs if mode is being transitioned
    for (Unit *unit : _factory->units) {
        if (unit->role) unit->delete_role();
    }

    return ModeIceConflict::from_desperation(new_mode, _factory, opp_factory);
}

bool ModeIceConflict::from_ice_superiority(Mode **new_mode, Factory *_factory) {
    LUX_ASSERT(new_mode);
    LUX_ASSERT(_factory);

    // Start of game only
    if (board.sim_step != 0) return false;

    bool ice1 = _factory->ice_cells[0]->man_dist_factory(_factory) == 1;
    for (Factory *opp_factory : board.opp->factories()) {
        // If non-desperate, avoid double-attacking opp factories
        bool already_handled = false;
        if (ice1) {
            for (Factory *own_factory : board.player->factories()) {
                ModeIceConflict *mode = ModeIceConflict::cast(own_factory->mode);
                if (mode && mode->opp_factory == opp_factory) already_handled = true;
            }
        }
        if (already_handled) continue;

        int dist = _factory->cell->man_dist(opp_factory->cell);
        bool vulnerable = opp_factory->cell->ice_vulnerable(_factory->cell);
        if (dist <= 10 && vulnerable) {
            *new_mode = new ModeIceConflict(_factory, opp_factory);
            return true;
        }
    }

    return false;
}

bool ModeIceConflict::from_desperation(Mode **new_mode, Factory *_factory,
                                       Factory *attacking_factory) {
    LUX_ASSERT(new_mode);
    LUX_ASSERT(_factory);

    bool defensive = false;
    bool desperate = false;

    if (attacking_factory) {
        defensive = true;
    } else {
        desperate = true;

        // Start of game only
        if (board.sim_step != 0) return false;

        // If there is adjacent ice, this factory is not desperate
        if (_factory->ice_cells[0]->man_dist_factory(_factory) <= 1) return false;
    }

    Factory *_opp_factory = (attacking_factory
                             ? attacking_factory
                             : _factory->cell->nearest_factory(board.opp));
    *new_mode = new ModeIceConflict(_factory, _opp_factory, defensive, desperate);
    return true;
}

void ModeIceConflict::print(ostream& os) const {
    string d = this->defensive ? "(d)" : "";
    os << "IceConflict[" << *this->factory << " -> " << *this->opp_factory << d << "]";
}

void ModeIceConflict::set() {
    Mode::set();
}

void ModeIceConflict::unset() {
    if (this->is_set()) {
        Mode::unset();
    }
}

bool ModeIceConflict::is_valid() {
    bool is_valid = this->opp_factory && this->opp_factory->alive();

    // Quickly abort non-critical ice conflict battles if there are too many
    if (is_valid
        && board.sim0()
        && this->offensive
        && this->factory->ice_cells[0]->man_dist_factory(this->factory) == 1
        && (this->opp_factory->ice_cells[0]->man_dist_factory(this->opp_factory) == 1
            || board.step >= 2)) {
        int ice_conflict_count = 0;
        int default_count = 0;
        ModeIceConflict *mode;
        for (Factory *f : board.player->factories()) {
            if ((mode = ModeIceConflict::cast(f->mode))
                && mode->opp_factory->alive()
                && mode->opp_factory->water > 15) {
                ice_conflict_count++;
            }
            else if (!f->mode || ModeDefault::cast(f->mode)) {
                default_count++;
            }
        }
        if (ice_conflict_count > 2 * default_count) {
            is_valid = false;
        }
    }

    // Invalidate defensive conflict when a second heavy arrives
    if (is_valid
        && board.sim0()
        && this->defensive
        && this->factory->heavies.size() > 1) {
        is_valid = false;
    }

    // Invalidate defensive conflict if nearest ice cell is unthreatened
    if (is_valid
        && board.sim0()
        && this->defensive
        && this->factory->heavies.size() == 1) {
        Unit *heavy_unit = this->factory->heavies.back();
        if (!heavy_unit->threat_units(this->factory->ice_cells[0], /*past_steps*/10)) {
            is_valid = false;
        }
    }

    // Invalidate defensive conflict if attacking factory has 0 remaining heavies
    if (is_valid
        && board.sim0()
        && this->defensive
        && this->opp_factory->heavies.empty()
        && this->opp_factory->total_metal() < g_heavy_cfg.METAL_COST) {
        is_valid = false;
    }

    if (!is_valid) {
        // Force most units to find new jobs if mode is being invalidated
        for (Unit *unit : this->factory->units) {
            // Let water transporters finish active delivery
            // Let blockade units continue to work if they think it's appropriate
            if (unit->role
                && !RoleWaterTransporter::cast(unit->role)
                && !RoleBlockade::cast(unit->role)) {
                unit->delete_role();
            }
        }

        // Ensure at least one ice cell is assigned to this factory
        map<Factory*, int> ice_cell_counts;
        for (Cell *ice_cell : board.ice_cells) {
            if (ice_cell->assigned_factory) ice_cell_counts[ice_cell->assigned_factory] += 1;
        }
        if (!ice_cell_counts.count(this->factory)) {
            for (Cell *ice_cell : this->factory->ice_cells) {
                if (!ice_cell->assigned_factory
                    || (ice_cell_counts.count(ice_cell->assigned_factory)
                        && ice_cell_counts[ice_cell->assigned_factory] > 1)) {
                    ice_cell->assigned_factory = this->factory;
                    break;
                }
            }
        }
    }

    return is_valid;
}

Role *ModeIceConflict::get_transition_role(Unit *unit) {
    return this->_get_transition_role(unit);
}

Role *ModeIceConflict::get_new_role(Unit *unit) {
    Role *r = NULL;
    bool success = false;
    if (unit->heavy) {
        success = (
            false
            || RoleAntagonizer::from_factory(&r, unit, this->opp_factory)
            );

        // If there is plenty of power, do normal stuff, otherwise conserve
        if (!success
            && this->factory->power >= 3000
            && this->factory->heavy_antagonizer_count >= 1) {
            r = this->_get_new_role(unit);
            success = true;
        }

        if (!success) {
            success = (
                false
                || RoleRecharge::from_unit(&r, unit)
                );
        }
    } else {
        Cell *heavy_ant_target_cell = NULL;
        for (Unit *unit : this->factory->heavies) {
            if (RoleAntagonizer::cast(unit->role)) {
                heavy_ant_target_cell = RoleAntagonizer::cast(unit->role)->target_cell;
                break;
            }
        }

        success = (
            false
            || RoleWaterTransporter::from_ice_conflict(&r, unit)
            || (board.step >= 3
                && RoleCow::from_custom_route(&r, unit, heavy_ant_target_cell, /*count*/1))
            || RoleAntagonizer::from_chain(&r, unit, /*dist*/12, /*count*/2)
            || (board.low_iceland()
                && RoleMiner::from_resource(&r, unit,Resource_ICE, /*dist*/20, /*cdist*/0, /*count*/3))
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/15, /*count*/1)
            || (this->factory->power >= 1000
                && RoleMiner::from_resource(&r, unit,Resource_ICE, /*dist*/20, /*cdist*/0, /*count*/1))
            );

        // If there is plenty of power, do normal stuff, otherwise conserve
        if (!success && this->factory->power >= 2500) {
            r = this->_get_new_role(unit);
            success = true;
        }

        if (!success) {
            success = (
                false
                || RoleRecharge::from_unit(&r, unit)
                );
        }
    }
    LUX_ASSERT(success);
    LUX_ASSERT(r);
    return r;
}

bool ModeIceConflict::build_heavy_next() {
    return this->_build_heavy_next();
}

bool ModeIceConflict::do_build() {
    return this->_do_build();
}

bool ModeIceConflict::do_water() {
    if (this->factory->water >= ALWAYS_WATER_THRESHOLD || board.sim_step >= END_PHASE) {
        return this->_do_water();
    }
    return false;
}

