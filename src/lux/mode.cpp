#include "lux/mode.hpp"

#include "lux/board.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/log.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_chain_transporter.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_defender.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pillager.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
#include "lux/unit.hpp"
using namespace std;


Mode::Mode(Factory *_factory) {
    this->factory = _factory;
    this->_set_step = -1;
}

Role *Mode::_get_transition_role(Unit *unit) {
    Role *r = NULL;
    bool success = false;
    if (unit->heavy) {
        success = (
            false
            || RoleRecharge::from_transition_low_power(&r, unit)
            || RoleRecharge::from_transition_low_water(&r, unit)
            || RoleAntagonizer::from_transition_destroy_factory(&r, unit, /*dist*/15)
            || RoleCow::from_transition_lichen_repair(&r, unit)
            || RoleAttacker::from_transition_low_power_attack(&r, unit)
            || RoleProtector::from_transition_power_transporter(&r, unit)
            || RoleProtector::from_transition_protect_ice_miner(&r, unit)
            || RoleRelocate::from_transition_assist_ice_conflict(&r, unit)
            || RoleMiner::from_transition_to_uncontested_ice(&r, unit)
            || RoleMiner::from_transition_to_ore(&r, unit, /*dist*/20, /*chain_dist*/5)
            || RoleAttacker::from_transition_defend_territory(&r, unit)
            || RolePowerTransporter::from_transition_protector(&r, unit)
            || RolePillager::from_transition_end_of_game(&r, unit)
            || RolePillager::from_transition_active_pillager(&r, unit, /*dist*/20)
            || RoleAntagonizer::from_transition_antagonizer_with_target_factory(&r, unit)
            || RoleMiner::from_transition_to_closer_ice(&r, unit)
            );
    } else {
        success = (
            false
            || RoleRecharge::from_transition_low_power(&r, unit)
            || RoleRecharge::from_transition_low_water(&r, unit)
            || RoleWaterTransporter::from_transition_ice_conflict(&r, unit)
            || RoleBlockade::from_transition_block_water_transporter(&r, unit)
            || RoleBlockade::from_transition_block_different_water_transporter(&r, unit)
            || RoleCow::from_transition_lichen_repair(&r, unit)
            || RoleAttacker::from_transition_low_power_attack(&r, unit)
            || RoleChainTransporter::from_transition_partial_chain(&r, unit, /*dist*/8)
            || RolePillager::from_transition_active_pillager(&r, unit, /*dist*/20)
            || RolePillager::from_transition_end_of_game(&r, unit)
            );
    }
    LUX_ASSERT(!success == !r);
    return r;
}

Role *Mode::_get_new_role(Unit *unit) {
    Role *r = NULL;
    bool success = false;
    if (unit->heavy) {
        success = (
            false
            || (board.sim_step >= 200
                && RoleCow::from_lichen_frontier(&r, unit, /*dist*/1, /*rubble*/20))
            || RoleMiner::from_resource(&r, unit, Resource_ORE, /*dist*/1, /*cdist*/5, /*count*/1)
            || RoleMiner::from_resource(&r, unit, Resource_ICE, /*dist*/1, /*cdist*/5, /*count*/1)
            || RoleCow::from_lichen_repair(&r, unit, /*dist*/4, /*count*/1)
            || RoleRelocate::from_power_surplus(&r, unit)

            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/20, /*count*/0, /*water*/100)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/15)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ORE, /*dist*/15)

            || RoleCow::from_lichen_bottleneck(&r, unit, /*dist*/1, /*min_rubble*/20)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/4, /*rubble*/40)
            || RolePillager::from_lichen(&r, unit, /*dist*/15, /*count*/1)
            || RolePillager::from_lichen_bottleneck(&r, unit, /*dist*/25)

            || RoleMiner::from_resource(&r, unit, Resource_ICE, /*dist*/1, /*cdist*/5, /*count*/3)
            // Defender if exists a heavy ore miner? max ~3?
            || RoleAttacker::from_transition_defend_territory(&r, unit, /*count*/1)
            || RoleMiner::from_resource(&r, unit, Resource_ICE, /*dist*/10, /*cdist*/0, /*count*/2)

            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/10, /*rubble*/40)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/10)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/15, /*rubble*/40)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/15)

            || RoleAttacker::from_transition_defend_territory(&r, unit, /*count*/3)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/30, /*count*/0, /*water*/50)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/25)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ORE, /*dist*/25)
            || RolePillager::from_lichen(&r, unit, /*dist*/25)
            || RolePillager::from_lichen_bottleneck(&r, unit, /*dist*/40)
            || RoleMiner::from_resource(&r, unit, Resource_ICE, /*dist*/10, /*cdist*/0, /*count*/3)

            || (unit->power >= 2950
                && RolePillager::from_lichen(&r, unit, /*dist*/45))
            || (board.sim_step >= 750
                && RoleRelocate::from_idle(&r, unit))
            || RoleDefender::from_unit(&r, unit, /*dist*/25)
            || RoleRecharge::from_unit(&r, unit)
            );
    } else {
        success = (
            false
            || RolePowerTransporter::from_miner(&r, unit)
            || RoleChainTransporter::from_miner(&r, unit)
            || RolePillager::from_lichen(&r, unit, /*dist*/20, /*count*/1)
            || RoleAntagonizer::from_chain(&r, unit, /*dist*/20, /*count*/2)
            || RoleRelocate::from_assist_ice_conflict(&r, unit)
            || RoleRelocate::from_ore_surplus(&r, unit)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/20, /*count*/0, /*water*/50)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ORE, /*dist*/15, /*count*/1)
            || RoleAntagonizer::from_mine(&r, unit, Resource_ICE, /*dist*/15, /*count*/1)

            || RoleCow::from_lowland_route(&r, unit, /*dist*/2, /*size*/50)
            || RoleCow::from_lowland_route(&r, unit, /*dist*/6, /*size*/100)
            || RoleCow::from_lowland_route(&r, unit, /*dist*/4, /*size*/15)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/10, /*rubble*/19, /*connected*/20)
            || RoleCow::from_lichen_bottleneck(&r, unit, /*dist*/10)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/15, /*rubble*/39, /*connected*/15)

            || RoleCow::from_resource_route(&r,unit, Resource_ORE, /*dist*/20, /*routes*/1, /*count*/4)

            || RoleCow::from_lowland_route(&r, unit, /*dist*/6, /*size*/50)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/10, /*rubble*/19)
            || RoleAttacker::from_transition_defend_territory(&r, unit, /*count*/4)
            || RoleAntagonizer::from_chain(&r, unit, /*dist*/20, /*count*/4)
            || RoleCow::from_lichen_repair(&r, unit, /*dist*/10)
            || (board.sim_step < 750
                && RoleRelocate::from_power_surplus(&r, unit))

            || RoleCow::from_lowland_route(&r, unit, /*dist*/8, /*size*/100, /*count*/2)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/15, /*rubble*/39)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/4, /*rubble*/79)
            || RoleCow::from_lichen_frontier(&r, unit, /*dist*/25)

            || RolePillager::from_lichen_bottleneck(&r, unit, /*dist*/40)
            || RoleAttacker::from_transition_defend_territory(&r, unit, /*count*/6)
            || RoleAntagonizer::from_chain(&r, unit, /*dist*/25)
            || RolePillager::from_lichen(&r, unit, /*dist*/25)
            || RoleCow::from_lichen_repair(&r, unit, /*dist*/15)

            || RoleMiner::from_resource(&r, unit, Resource_ORE, /*dist*/10, /*cdist*/0, /*count*/3)
            || RoleAttacker::from_transition_defend_territory(&r, unit, /*count*/8)
            || RoleCow::from_resource_route(&r, unit, Resource_ICE,/*dist*/10, /*routes*/3, /*count*/4)

            || (board.sim_step >= 750
                && RoleRelocate::from_idle(&r, unit))
            || RoleDefender::from_unit(&r, unit, /*dist*/25)
            || RoleRecharge::from_unit(&r, unit)
            );
    }
    LUX_ASSERT(success);
    LUX_ASSERT(r);
    return r;
}

// TODO: consider # of chain miners (and # of chain miners still needed)
bool Mode::_build_heavy_next() {
    if (board.sim_step >= END_PHASE + 30) return false;

    int light_lim = 14 + board.sim_step / 100;
    int heavy_count = this->factory->heavies.size();
    int light_count = this->factory->lights.size();

    // If ore miner is chained, pump out lights after 2 heavies
    if (heavy_count >= 2
        && light_count < light_lim) {
        RoleMiner *role = NULL;
        for (Unit *u : this->factory->heavies) {
            if ((role = RoleMiner::cast(u->role))
                && role->resource_cell->ore
                && (role->_transporters_exist())) return false;
        }
    }

    return (light_count >= light_lim
            || light_count >= heavy_count * 5);
}

bool Mode::_do_build() {
    if (board.sim_step >= ICE_RUSH_PHASE) {
        return false;
    }

    if (this->factory->can_build_heavy()) {
        this->factory->do_build_heavy();
        return true;
    } else if (factory->can_build_light()
               && (!this->build_heavy_next()
                   || this->factory->water <= 1)) {
        factory->do_build_light();
        return true;
    }

    return false;
}

bool Mode::_do_water() {
    this->factory->update_lichen_info();  // Call again - digging can affect lichen_growth_cells

    /*if (this->factory->can_water()
        && this->factory->water >= 100) {
        this->factory->do_water();
        return true;
    }
    return false;*/

    // Always water last step, even if fatal
    if (board.sim_step == 999) {
        this->factory->do_water();
        return true;
    }

    // Skip watering if no-op
    int water_cost = this->factory->water_cost();
    if (water_cost == 0 || !this->factory->can_water()) {
        return false;
    }

    // Skip watering if not growing anywhere new
    if (board.sim_step < END_PHASE
        && this->factory->lichen_flat_boundary_cells.empty()) {
        int min_growth_cell_lichen = INT_MAX;
        for (Cell *c : this->factory->lichen_growth_cells) {
            if (c->lichen < min_growth_cell_lichen) min_growth_cell_lichen = c->lichen;
        }
        if (min_growth_cell_lichen >= 15) {
            return false;
        }
    }

    // Go for broke at the end
    int water = this->factory->total_water();
    int broke_water_threshold = (  1                           * (1 + water_cost)
                                 + (1000 - board.sim_step - 2) * (1 + water_cost + 1)
                                 + 1                           * 0);
    if (water >= broke_water_threshold) {
        this->factory->do_water();
        return true;
    }

    // Skip watering if would put us below safety baseline
    int water_remaining = water - (1 + water_cost);
    if (water_remaining < NEVER_WATER_THRESHOLD) {
        return false;
    }

    // Always water above some threshold, this ensures we don't accidently over-store
    // If above some threshold, but water income is zero/low, only water every other turn
    // No need to grow if we have no income, but should try to hold steady
    if (water_remaining >= USUALLY_WATER_THRESHOLD) {
        if (water_remaining >= ALWAYS_WATER_THRESHOLD
            || this->factory->heavy_ice_miner_count > 0
            || board.sim_step % 2 == 0) {
            this->factory->do_water();
            return true;
        }
    }

    // Water pseudo-randomly based on income/cost comparison
    double water_income = this->factory->water_income();
    double percent_do_water = (water_income - 1) / water_cost;
    //LUX_LOG("do_water " << *this->factory << ' ' << water_income << ' '
    //        << water_cost << ' ' << percent_do_water);
    if (prandom(board.sim_step + this->factory->id, percent_do_water)) {
        this->factory->do_water();
        return true;
    }

    return false;
}

void Mode::set() {
    this->_set_step = 1000 * board.step + board.sim_step;
}

void Mode::unset() {
    this->_set_step = -1;
}

bool Mode::is_set() {
    return this->_set_step == 1000 * board.step + board.sim_step;
}
