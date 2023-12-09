#pragma once


#define DO_ALL(ROLE) \
    void do_ ## ROLE ## _move(bool heavy); \
    void do_ ## ROLE ## _dig(bool heavy); \
    void do_ ## ROLE ## _transfer(bool heavy); \
    void do_ ## ROLE ## _pickup(bool heavy)


typedef struct UnitGroup {
    int step;

    // ~~~ Methods:

    void finalize();

    DO_ALL(antagonizer);
    DO_ALL(attacker);
    DO_ALL(blockade);
    DO_ALL(chain_transporter);
    DO_ALL(cow);
    DO_ALL(defender);
    DO_ALL(miner);
    DO_ALL(pillager);
    DO_ALL(pincer);
    DO_ALL(power_transporter);
    DO_ALL(protector);
    DO_ALL(recharge);
    DO_ALL(relocate);
    DO_ALL(water_transporter);

    void do_move_998();
    void do_move_999();
    void do_dig_999();

    void do_move_to_exploding_factory(bool heavy);
    void do_pickup_resource_from_exploding_factory(bool heavy);
    void do_attack_trapped_unit_move(bool heavy);
    void do_move_win_collision(bool heavy);

    void do_blockade_move(bool heavy, bool primary, bool engaged);

    void do_chain_transporter_last_chain_move(bool heavy);
    void do_chain_transporter_threatened_move(bool heavy, bool last_chain_only = false);
    void do_chain_transporter_rx_no_move(bool heavy);
    void do_chain_transporter_ice_miner_pickup(bool heavy);
    void do_chain_transporter_special_transfer(bool heavy);  // can update a locked-in no-move

    void do_miner_protected_move(bool heavy);
    void do_miner_protected_dig(bool heavy);
    void do_miner_protected_transfer(bool heavy);
    void do_miner_protected_pickup(bool heavy);

    void do_miner_with_transporters_pickup(bool heavy);
    void do_miner_with_transporters_no_move(bool heavy);

    void do_pillager_dangerous_dig(bool heavy);

    void do_power_transporter_ice_miner_pickup(bool heavy);

    void do_water_transporter_emergency_move(bool heavy);

    void do_no_move(bool heavy);
} UnitGroup;


#undef DO_ALL
