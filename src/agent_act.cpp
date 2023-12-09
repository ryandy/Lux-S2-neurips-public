#include "agent.hpp"

#include <string>

#include "lux/board.hpp"
#include "lux/defs.hpp"
#include "lux/exception.hpp"
#include "lux/factory_group.hpp"
#include "lux/log.hpp"
#include "lux/unit_group.hpp"
using namespace std;


json Agent::act() {
    double start_time = get_time();
    string board_summary = board.summary();

    double max_time = g_prod ? MAX_TIME_PROD : MAX_TIME_DEV;
    int future_sim = g_prod ? FUTURE_SIM_PROD : FUTURE_SIM_DEV;

    for (int i = 0; i < future_sim; i++) {
        if (board.sim_step == 1000) break;
        LUX_LOG_DEBUG("AA A");
        if (i == 0) board.save_begin();
        board.begin_step_simulation();

        UnitGroup ugroup{.step = board.step + i};
        FactoryGroup fgroup{.step = board.step + i};

        ugroup.do_move_998();
        ugroup.do_dig_999();
        ugroup.do_move_999();
        ugroup.do_move_to_exploding_factory(true);
        ugroup.do_move_to_exploding_factory(false);
        ugroup.do_pickup_resource_from_exploding_factory(true);
        ugroup.do_pickup_resource_from_exploding_factory(false);

        ugroup.do_attack_trapped_unit_move(true);
        ugroup.do_pincer_move(true);

        ugroup.do_miner_protected_transfer(true);
        ugroup.do_miner_protected_dig(true);
        ugroup.do_miner_protected_pickup(true);
        ugroup.do_miner_protected_move(true);

        ugroup.do_protector_transfer(true);
        ugroup.do_protector_pickup(true);
        ugroup.do_protector_move(true);

        // Heavy miners are high priority except for picking up power, which is lower than transporters
        ugroup.do_chain_transporter_last_chain_move(false);
        ugroup.do_chain_transporter_threatened_move(false, /*last_chain*/true);
        ugroup.do_miner_transfer(true);
        ugroup.do_miner_dig(true);
        ugroup.do_miner_with_transporters_pickup(true);
        ugroup.do_miner_move(true);
        ugroup.do_miner_with_transporters_no_move(true);
        ugroup.do_chain_transporter_rx_no_move(false);  // lock in no-move if receiving something

        ugroup.do_recharge_transfer(true);
        ugroup.do_recharge_move(true);

        // ~~~ LIGHT ~~~ blockade and water transporters
        ugroup.do_blockade_transfer(false);
        ugroup.do_blockade_pickup(false);
        ugroup.do_water_transporter_transfer(false);
        ugroup.do_water_transporter_pickup(false);
        ugroup.do_water_transporter_emergency_move(false);
        ugroup.do_blockade_move(false, /*primary*/true, /*engaged*/true);
        ugroup.do_blockade_move(false, /*primary*/false, /*engaged*/true);
        ugroup.do_blockade_move(false, /*primary*/true, /*engaged*/false);
        ugroup.do_blockade_move(false, /*primary*/false, /*engaged*/false);
        ugroup.do_water_transporter_move(false);

        ugroup.do_move_win_collision(true);

        // ~~~ LIGHT ~~~ attack trapped
        ugroup.do_attack_trapped_unit_move(false);

        ugroup.do_power_transporter_transfer(true);
        ugroup.do_power_transporter_ice_miner_pickup(true);
        ugroup.do_power_transporter_pickup(true);
        ugroup.do_power_transporter_move(true);

        // ~~~ LIGHT ~~~ power transporters
        ugroup.do_power_transporter_transfer(false);
        ugroup.do_power_transporter_ice_miner_pickup(false);
        ugroup.do_power_transporter_pickup(false);
        ugroup.do_power_transporter_move(false);

        // ~~~ LIGHT ~~~ chain transporters
        ugroup.do_chain_transporter_move(false);  // try to move first to establish position for tx's
        ugroup.do_chain_transporter_threatened_move(false);
        ugroup.do_chain_transporter_special_transfer(false);
        ugroup.do_chain_transporter_ice_miner_pickup(false);
        ugroup.do_chain_transporter_pickup(false);
        ugroup.do_chain_transporter_dig(false);  // rare
        ugroup.do_chain_transporter_rx_no_move(false);  // lock in no-move if receiving something

        // Heavy miner power pickup
        ugroup.do_miner_pickup(true);  // TODO: don't want light transpos to push these heavies around

        ugroup.do_attacker_transfer(true);
        ugroup.do_attacker_pickup(true);
        ugroup.do_attacker_move(true);

        ugroup.do_relocate_transfer(true);
        ugroup.do_relocate_pickup(true);
        ugroup.do_relocate_move(true);

        ugroup.do_pillager_dig(true);
        ugroup.do_pillager_dangerous_dig(true);
        ugroup.do_pillager_transfer(true);
        ugroup.do_pillager_pickup(true);
        ugroup.do_pillager_move(true);

        ugroup.do_antagonizer_transfer(true);
        ugroup.do_antagonizer_pickup(true);
        ugroup.do_antagonizer_dig(true);
        ugroup.do_antagonizer_move(true);

        ugroup.do_cow_dig(true);
        ugroup.do_cow_transfer(true);
        ugroup.do_cow_pickup(true);
        ugroup.do_cow_move(true);

        ugroup.do_defender_dig(true);
        ugroup.do_defender_transfer(true);
        ugroup.do_defender_pickup(true);
        ugroup.do_defender_move(true);

        LUX_LOG_DEBUG("AA B");
        fgroup.do_build();
        ugroup.do_no_move(true);

        // ^ Heavy / Light v

        ugroup.do_move_win_collision(false);

        ugroup.do_miner_transfer(false);
        ugroup.do_miner_dig(false);
        ugroup.do_miner_pickup(false);
        ugroup.do_miner_move(false);

        ugroup.do_recharge_transfer(false);
        ugroup.do_recharge_move(false);

        ugroup.do_relocate_transfer(false);
        ugroup.do_relocate_pickup(false);
        ugroup.do_relocate_move(false);

        ugroup.do_attacker_transfer(false);
        ugroup.do_attacker_pickup(false);
        ugroup.do_attacker_move(false);

        ugroup.do_pillager_dig(false);
        ugroup.do_pillager_dangerous_dig(false);
        ugroup.do_pillager_transfer(false);
        ugroup.do_pillager_pickup(false);
        ugroup.do_pillager_move(false);

        ugroup.do_antagonizer_transfer(false);
        ugroup.do_antagonizer_pickup(false);
        ugroup.do_antagonizer_dig(false);
        ugroup.do_antagonizer_move(false);

        ugroup.do_cow_dig(false);
        ugroup.do_cow_transfer(false);
        ugroup.do_cow_pickup(false);
        ugroup.do_cow_move(false);

        ugroup.do_defender_transfer(true);  // Try again now that light recharges have moved
        ugroup.do_defender_transfer(false);
        ugroup.do_defender_dig(false);
        ugroup.do_defender_pickup(false);
        ugroup.do_defender_move(false);

        ugroup.do_no_move(false);
        fgroup.do_water();
        fgroup.do_none();

        LUX_LOG_DEBUG("AA C");
        ugroup.finalize();
        if (i == 0) fgroup.finalize();

        board.end_step_simulation();
        if (i == 0) board.save_end();

        // Exit early if not enough time to finish another loop
        double elapsed_time = get_time() - start_time;
        double time_per_step = elapsed_time / (i + 1);
        if (elapsed_time + time_per_step > max_time) break;

        LUX_LOG_DEBUG("AA D");
        board.sim_step += 1;
    }

    json actions = json::object();
    board.player->get_new_actions(&actions);

    if (board.step % 20 == 0 || board.step == 999) LUX_LOG(board_summary);

    board.load();
    return actions;
}
