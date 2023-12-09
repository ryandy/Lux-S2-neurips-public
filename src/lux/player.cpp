#include "lux/player.hpp"

#include "lux/board.hpp"
#include "lux/factory.hpp"
#include "lux/log.hpp"
#include "lux/team.hpp"
#include "lux/unit.hpp"
using namespace std;


void Player::init(Team *_team, bool is_player0, json &strains_info) {
    this->team = _team;
    this->id = (is_player0 ? 0 : 1);
    this->strains = 0;
    for (int strain : strains_info) {
	this->strains |= (1 << strain);
    }

    this->_units_step = -1;
    this->_factories_step = -1;
}

list<Unit*> &Player::units() {
    if (this->_units_step != board.step) {  // Cache is outdated step-to-step
        this->_units_step = board.step;
        this->_units.clear();
        for (Unit &unit : board.units) {
            if (unit.alive()  // always needs to be first check because some of units are placeholders
                && unit.player == this
                && unit.build_step <= board.sim_step) {  // no future units created this step
                this->_units.push_back(&unit);
            }
        }
    }
    return this->_units;
}

list<Factory*> &Player::factories() {
    if (this->_factories_step != board.step) {
	this->_factories_step = board.step;
	this->_factories.clear();
        for (Factory &factory : board.factories) {
            if (factory.player == this && factory.alive()) {
                this->_factories.push_back(&factory);
            }
        }
    }
    return this->_factories;
}

void Player::add_new_units() {
    if (this->_units_step == board.step) {
        int unit_id = this->_units.empty() ? -1 : this->_units.back()->id;
        for (size_t i = unit_id + 1; i < board.units.size(); i++) {
            if (board.units[i].player == this
                && board.units[i].alive()) {  // no dead units
                this->_units.push_back(&board.units[i]);
            }
        }
    }
}

void Player::get_new_actions(json *actions) {
    for (Unit *unit : this->units()) {
	if (unit->build_step > board.step) break;  // Future unit
	if (unit->power_init >= unit->cfg->ACTION_QUEUE_POWER_COST
	    && ((unit->aq_len == 0 && !unit->new_action_queue[0].is_idle())
		|| (unit->aq_len > 0 && !unit->action_queue[0].equal(&unit->new_action_queue[0])))) {
            unit->action_queue_update_count++;
            unit->compress_new_action_queue();
	    for (ActionSpec &spec : unit->new_action_queue) {
		(*actions)[unit->id_str()].push_back(spec);
	    }
            // Log inefficient AQ overwrites
            if (unit->aq_len > 0  // Ok to overwrite empty queue
                && unit->action_queue[0].repeat == 0  // Ok to overwrite tail-end repeat actions
                && (unit->threat_unit_steps.size() == 0  // Ok to overwrite due to opp threat
                    || unit->threat_unit_steps.back().second != board.step)) {
                int nonrepeating_len = 0;
                for (; nonrepeating_len < unit->aq_len; nonrepeating_len++) {
                    if (unit->action_queue[nonrepeating_len].repeat > 0) break;
                }
                //LUX_LOG("AQ overwrite.. " << *unit << " (" << nonrepeating_len << ") "
                //        << unit->action_queue[0] << " -> "
                //        << unit->new_action_queue[0]);
            }
	}
    }

    for (Factory *factory : this->factories()) {
        if (factory->new_action != FactoryAction_NONE) {
            (*actions)[factory->id_str()] = factory->new_action;
        }
    }
}
