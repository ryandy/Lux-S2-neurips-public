#include "lux/unit_group.hpp"

#include "lux/action.hpp"
#include "lux/board.hpp"
#include "lux/log.hpp"
#include "lux/role_antagonizer.hpp"
#include "lux/role_attacker.hpp"
#include "lux/role_blockade.hpp"
#include "lux/role_chain_transporter.hpp"
#include "lux/role_cow.hpp"
#include "lux/role_defender.hpp"
#include "lux/role_miner.hpp"
#include "lux/role_pillager.hpp"
#include "lux/role_pincer.hpp"
#include "lux/role_power_transporter.hpp"
#include "lux/role_protector.hpp"
#include "lux/role_recharge.hpp"
#include "lux/role_relocate.hpp"
#include "lux/role_water_transporter.hpp"
using namespace std;


#define DO_ONE_SAFE(ROLE_CAP, ROLE, ACTION)                             \
    void UnitGroup::do_ ## ROLE ## _ ## ACTION(bool heavy) {            \
        for (Unit *unit : board.player->units()) {                      \
            if (unit->last_action_step < this->step                     \
                && unit->heavy == heavy                                 \
                && typeid(*unit->role) == typeid(Role ## ROLE_CAP)      \
                && unit->move_is_safe_from_friendly_fire(unit->cell())  \
                && unit->move_risk(unit->cell()) <= 0                   \
                && unit->role->do_ ## ACTION()) {                       \
                unit->last_action_step = this->step; }}}
#define DO_ONE(ROLE_CAP, ROLE, ACTION)                                  \
    void UnitGroup::do_ ## ROLE ## _ ## ACTION(bool heavy) {            \
        for (Unit *unit : board.player->units()) {                      \
            if (unit->last_action_step < this->step                     \
                && unit->heavy == heavy                                 \
                && typeid(*unit->role) == typeid(Role ## ROLE_CAP)      \
                && unit->role->do_ ## ACTION()) {                       \
                unit->last_action_step = this->step; }}}
#define DO_ALL(ROLE_CAP, ROLE)                                  \
    DO_ONE(ROLE_CAP, ROLE, move)                                \
    DO_ONE_SAFE(ROLE_CAP, ROLE, dig)                            \
    DO_ONE_SAFE(ROLE_CAP, ROLE, transfer)                       \
    DO_ONE_SAFE(ROLE_CAP, ROLE, pickup)


void UnitGroup::finalize() {
    for (Unit *unit : board.player->units()) {
        // Record step that AQ cost will be paid
	if (unit->need_action_queue_cost(&unit->action)) {
            if (unit->power >= unit->cfg->ACTION_QUEUE_POWER_COST) {
                unit->action_queue_cost_step = this->step;
                unit->action_queue_cost_iou = false;
            } else {
                unit->action_queue_cost_iou = true;
            }
        }
	unit->new_action_queue.push_back(unit->action);
    }
}

DO_ALL(Antagonizer, antagonizer)
DO_ALL(Attacker, attacker)
DO_ALL(Blockade, blockade)
DO_ALL(ChainTransporter, chain_transporter)
DO_ALL(Cow, cow)
DO_ALL(Defender, defender)
DO_ALL(Miner, miner)
DO_ALL(Pillager, pillager)
DO_ALL(Pincer, pincer)
DO_ALL(PowerTransporter, power_transporter)
DO_ALL(Protector, protector)
DO_ALL(Recharge, recharge)
DO_ALL(Relocate, relocate)
DO_ALL(WaterTransporter, water_transporter)

void UnitGroup::do_move_998() {
    if (board.sim_step != 998) return;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->role->_do_move_998()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_move_999() {
    if (board.sim_step != 999) return;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->role->_do_move_999()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_dig_999() {
    if (board.sim_step != 999) return;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->role->_do_dig_999()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_move_to_exploding_factory(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && unit->role->_do_move_to_exploding_factory()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_pickup_resource_from_exploding_factory(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && unit->move_is_safe_from_friendly_fire(unit->cell())
            && unit->role->_do_pickup_resource_from_exploding_factory()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_attack_trapped_unit_move(bool heavy) {
    if (!board.sim0()) return;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && unit->role->_do_move_attack_trapped_unit()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_move_win_collision(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (RoleCow::cast(unit->role)
                || RoleDefender::cast(unit->role)
                || (RoleMiner::cast(unit->role)
                    && !unit->heavy)
                || RolePillager::cast(unit->role)
                || RolePowerTransporter::cast(unit->role)
                || RoleRelocate::cast(unit->role))
            && unit->role->_do_move_win_collision()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_blockade_move(bool heavy, bool primary, bool engaged) {
    RoleBlockade *role;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleBlockade::cast(unit->role))) {
            bool a = role->is_primary();
            bool b = role->is_engaged();
            //LUX_LOG(*unit << " UG::do_blockade_move " << a << ' ' << b);
            if (a == primary
                && b == engaged
                && unit->role->do_move()) {
                unit->last_action_step = this->step;
            }
        }
    }
}

void UnitGroup::do_chain_transporter_last_chain_move(bool heavy) {
    for (Unit *unit : board.player->units()) {
        RoleChainTransporter *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleChainTransporter::cast(unit->role))
            && role->do_move_last_chain()) {
            if (unit->_log_cond()) LUX_LOG(*unit << " CT last chain move");
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_chain_transporter_threatened_move(bool heavy, bool last_chain_only) {
    for (Unit *unit : board.player->units()) {
        RoleChainTransporter *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleChainTransporter::cast(unit->role))
            && role->do_move_if_threatened(last_chain_only)) {
            if (unit->_log_cond()) LUX_LOG(*unit << " CT threat move");
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_chain_transporter_rx_no_move(bool heavy) {
    for (Unit *unit : board.player->units()) {
        RoleChainTransporter *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleChainTransporter::cast(unit->role))
            && role->do_no_move_if_receiving()) {
            if (unit->_log_cond()) LUX_LOG(*unit << " CT rx no move");
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_chain_transporter_ice_miner_pickup(bool heavy) {
    RoleChainTransporter *role;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleChainTransporter::cast(unit->role))
            && RoleMiner::cast(role->target_unit->role)
            && RoleMiner::cast(role->target_unit->role)->resource_cell->ice
            && role->do_pickup()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_chain_transporter_special_transfer(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if ((unit->last_action_step < this->step
             || (unit->action.action == UnitAction_MOVE
                 && unit->action.direction == Direction_CENTER))  // update a no-move
            && unit->heavy == heavy
            && typeid(*unit->role) == typeid(RoleChainTransporter)
            //&& unit->move_is_safe_from_friendly_fire(unit->cell())  // causes assert
            && unit->move_risk(unit->cell()) <= 0
            && unit->role->do_transfer()) {
            if (unit->last_action_step == this->step
                && unit->_log_cond()) LUX_LOG(*unit << " CT special transfer");
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_miner_protected_move(bool heavy) {
    RoleMiner *role_miner;
    RoleProtector *role_protector;
    for (Unit *unit : board.player->units()) {
        if (board.sim0()
            && unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role_miner = RoleMiner::cast(unit->role))
            && role_miner->protector
            && (role_protector = RoleProtector::cast(role_miner->protector->role))) {
            // Protected move: go to resource cell
            if (role_miner->goal_type == 'c'
                && unit->cell()->man_dist(role_miner->resource_cell) <= 1
                && role_protector->is_covering()
                && unit->role->_do_move_direct(role_miner->resource_cell)) {
                if (unit->_log_cond()) LUX_LOG(*unit << " protected miner move (go to resource)");
                unit->last_action_step = this->step;
            }
            // Take protector's factory cell (easier to move back into mining position this way)
            else if (role_miner->goal_type == 'f'
                     && unit->cell()->man_dist(role_protector->factory_cell) <= 1
                     && unit->role->_do_move_direct(role_protector->factory_cell)) {
                if (unit->_log_cond()) LUX_LOG(*unit << " protected miner move (go to factory)");
                unit->last_action_step = this->step;
            }
            // Lock in move now so that protector doesn't push miner off for no reason
            // This allows miner to stand still temporarily e.g. for a standoff
            else if (role_miner->goal_type == 'c'
                     && unit->cell() == role_protector->factory_cell
                     && unit->role->_do_move(role_miner->resource_cell)) {
                if (unit->_log_cond()) LUX_LOG(*unit << " protected miner move (go to resource2)");
                unit->last_action_step = this->step;
            }
        }
    }
}

void UnitGroup::do_miner_protected_dig(bool heavy) {
    RoleMiner *role_miner;
    RoleProtector *role_protector;
    for (Unit *unit : board.player->units()) {
        if (board.sim0()
            && unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role_miner = RoleMiner::cast(unit->role))
            && role_miner->protector
            && unit->cell() == role_miner->resource_cell
            && (role_protector = RoleProtector::cast(role_miner->protector->role))
            && unit->move_is_safe_from_friendly_fire(unit->cell())
            && role_protector->is_covering()
            && unit->role->do_dig()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_miner_protected_transfer(bool heavy) {
    RoleMiner *role_miner;
    RoleProtector *role_protector;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role_miner = RoleMiner::cast(unit->role))
            && role_miner->protector
            && (role_protector = RoleProtector::cast(role_miner->protector->role))
            && unit->move_is_safe_from_friendly_fire(unit->cell())) {
            // Protected transfer
            if (board.sim0()
                && unit->cell() == role_miner->resource_cell
                && role_protector->is_covering()
                && unit->role->do_transfer()) {
                if (unit->_log_cond()) LUX_LOG(*unit << " protected miner transfer A");
                unit->last_action_step = this->step;
            }
            // On-factory transfer
            else if (unit->cell()->factory == role_miner->factory
                     && unit->role->do_transfer()) {
                if (unit->_log_cond()) LUX_LOG(*unit << " protected miner transfer B");
                unit->last_action_step = this->step;
            }
        }
    }
}

void UnitGroup::do_miner_protected_pickup(bool heavy) {
    RoleMiner *role_miner;
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role_miner = RoleMiner::cast(unit->role))
            && role_miner->protector
            && unit->move_is_safe_from_friendly_fire(unit->cell())
            && unit->role->do_pickup()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_miner_with_transporters_pickup(bool heavy) {
    for (Unit *unit : board.player->units()) {
        RoleMiner *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleMiner::cast(unit->role))
            && role->_transporters_exist()
            && role->do_pickup()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_miner_with_transporters_no_move(bool heavy) {
    for (Unit *unit : board.player->units()) {
        RoleMiner *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RoleMiner::cast(unit->role))
            && role->_transporters_exist()) {
            unit->role->_do_no_move();
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_pillager_dangerous_dig(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && typeid(*unit->role) == typeid(RolePillager)
            && ((heavy && (board.sim_step >= 980 || unit->power < 210))
                || (!heavy && board.sim_step >= END_PHASE))
            && unit->move_is_safe_from_friendly_fire(unit->cell())
            //&& unit->move_risk(unit->cell()) <= 0
            && unit->role->do_dig()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_power_transporter_ice_miner_pickup(bool heavy) {
    for (Unit *unit : board.player->units()) {
        RolePowerTransporter *role = NULL;
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && (role = RolePowerTransporter::cast(unit->role))
            && RoleMiner::cast(role->target_unit->role)
            && RoleMiner::cast(role->target_unit->role)->resource_cell->ice
            && role->do_pickup()) {
            unit->last_action_step = this->step;
        }
    }
}

void UnitGroup::do_water_transporter_emergency_move(bool heavy) {
    for (Unit *unit : board.player->units()) {
        if (unit->last_action_step < this->step
            && unit->heavy == heavy
            && RoleWaterTransporter::cast(unit->role)
            && unit->water > 0) {
            int dist = unit->cell()->man_dist_factory(unit->assigned_factory);
            if (unit->assigned_factory->water - 5 < dist
                && dist <= unit->assigned_factory->water + 1
                && unit->role->do_move()) {
                if (unit->_log_cond()) LUX_LOG(*unit << " WT emergency move");
                unit->last_action_step = this->step;
            }
        }
    }
}

void UnitGroup::do_no_move(bool heavy) {
    for (Unit *unit : board.player->units()) {
	if (unit->last_action_step < this->step && unit->heavy == heavy) {
            // Try not to move from current cell, taking into account potential risks.
            // If no power to do so, do a true no-move.
            //if (unit->_log_cond()) LUX_LOG("UnitGroup::do_no_move A " << *unit);
            unit->role->_do_no_move();
            unit->last_action_step = this->step;
        }
    }
}
