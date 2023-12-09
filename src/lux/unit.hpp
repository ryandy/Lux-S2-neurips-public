#pragma once

#include "lux/action.hpp"
#include "lux/json.hpp"

#include <cstdint>
#include <functional>  // function
#include <iostream>
#include <string>
#include <vector>


struct Cell;
struct Factory;
struct Player;
struct Role;

typedef struct UnitConfig {
    int ACTION_QUEUE_POWER_COST;
    int BATTERY_CAPACITY;
    int CARGO_SPACE;
    int CHARGE;
    int DIG_COST;
    int DIG_LICHEN_REMOVED;
    int DIG_RESOURCE_GAIN;
    int DIG_RUBBLE_REMOVED;
    int INIT_POWER;
    int METAL_COST;
    int MOVE_COST;
    int POWER_COST;
    int RUBBLE_AFTER_DESTRUCTION;
    double  RUBBLE_MOVEMENT_COST;
    int SELF_DESTRUCT_COST;
    int RAZE_COST;  // equal to self-destruct for lights and dig for heavies
} UnitConfig;
extern UnitConfig g_light_cfg;
extern UnitConfig g_heavy_cfg;

constexpr int UNIT_AQ_BUF_LEN = 20;

typedef struct Unit {
    int16_t id;
    struct Player *player;
    int16_t x;
    int16_t y;
    bool heavy;
    UnitConfig *cfg;

    int8_t raq_len;
    int8_t aq_len;
    ActionSpec raw_action_queue[UNIT_AQ_BUF_LEN];
    ActionSpec action_queue[UNIT_AQ_BUF_LEN];

    int16_t ice;
    int16_t ore;
    int16_t water;
    int16_t metal;
    int16_t power;
    int16_t power_init;

    int16_t x_delta;
    int16_t y_delta;
    int16_t ice_delta;
    int16_t ore_delta;
    int16_t water_delta;
    int16_t metal_delta;
    int16_t power_delta;

    int build_step;
    int alive_step;

    struct Role *role;
    std::vector<struct Cell*> route;

    struct Unit *assigned_unit;  // (attacker) unit assigned to this (opp) unit
    struct Factory *assigned_factory;  // factory that "owns" this unit
    struct Factory *last_factory;  // most recent factory visited

    struct Unit *antagonizer_unit;  // updated beginning of each step
    struct Unit *oscillating_unit;  // only set for opp units
    bool is_trapped;  // only set for opp units - no move is safe

    bool low_power;
    int low_power_threshold;  // Power necessary to return to factory - may be an overestimate
    std::vector<struct Cell*> low_power_route;

    int last_action_step;
    ActionSpec action;  // temporary holder for next action before being added to new_action_queue
    std::vector<ActionSpec> new_action_queue;
    int action_queue_cost_step;  // step that AQ cost will be paid or INT_MAX
    bool action_queue_cost_iou;  // AQ differs and cost must be paid ASAP
    int action_queue_update_count;

    std::vector<struct Cell*> cell_history;  // use index = (board.step - this->build_step)
    std::vector<std::pair<struct Unit*, int> > threat_unit_steps;
    std::vector<std::pair<struct Cell*, int> > mine_cell_steps;
    std::vector<std::pair<struct Cell*, int> > future_mine_cell_steps;

    int16_t prev_ice;
    int16_t prev_ore;
    int16_t prev_water;
    int16_t prev_prev_water;
    int8_t prev_rubble;
    int8_t prev_lichen_strain;

    struct Role *_save_role;
    std::vector<struct Cell*> _save_route;
    struct Factory *_save_assigned_factory;

    // ~~~ Methods:

    void init(int unit_id, int player_id, int x, int y, bool heavy, int step,
	      int ice, int ore, int water, int metal, int power, json *aq_json);
    void save_end();
    void load();
    void handle_destruction();
    bool alive();
    bool _log_cond();

    std::string id_str();
    struct Cell *cell();
    struct Cell *cell_next();
    struct Cell *cell_at(int step);

    void update_stats_begin();
    void update_assigned_factory(struct Factory *new_factory = NULL);
    void update_antagonizer_unit();
    void update_is_trapped();

    int steps_until_power(int amount);
    void expand_raw_action_queue();
    void compress_new_action_queue();
    void normalize_action_queue();  // change next action in queue to no-op if illegal/unaffordable

    void new_role(struct Role *new_role);
    void delete_role();
    void update_goal();

    int power_gain(int step = -1);
    int power_gain(int step, int end_step);
    bool need_action_queue_cost(ActionSpec *spec);

    void set_unit_assignment(struct Unit *unit);
    void unset_unit_assignment(struct Unit *unit);

    void future_route(struct Factory *dest_factory, int max_len, std::vector<struct Cell*> *route);

    void update_low_power();

    bool is_stationary(int steps);
    bool is_chain();
    bool threat_units(struct Cell *cell, int past_steps = 3, int max_radius = 2,
                      bool ignore_heavies = false, bool ignore_lights = false,
                      std::vector<Unit*> *threat_units = NULL);

    int standoff_steps(struct Unit *opp_unit);
    bool break_standoff(struct Unit *opp_unit);  // standoff exists and we should prandomly break it

    int move_basic_cost(struct Cell *move_cell,  // no AQ cost
                        int move_cost = -1, double rubble_movement_cost = -1);
    int move_count(bool include_center);
    bool move_is_safe_from_friendly_fire(struct Cell *move_cell);
    int move_risk(struct Cell *move_cell,
                  std::vector<struct Unit*> *threat_units = NULL,
                  bool all_collisions = false);
    int move_risk(struct Cell *move_cell, struct Unit *opp_unit,
                  std::vector<struct Unit*> *threat_units = NULL);

    void register_move(Direction direction);
    Direction move_direction(struct Cell *goal_cell);
    int move_cost(Direction direction);
    int move_cost(struct Cell *neighbor);
    void do_move(Direction direction, bool no_move = false);

    int dig_cost();
    void do_dig();

    int self_destruct_cost();
    void do_self_destruct();

    int transfer_cost(struct Cell *neighbor, Resource resource, int amount);
    void do_transfer(struct Cell *neighbor, Resource resource, int amount,
                     struct Unit *rx_unit_override = NULL);

    int pickup_cost(Resource resource, int amount);
    void do_pickup(Resource resource, int amount);

    friend std::ostream &operator<<(std::ostream &os, const struct Unit &u);
} Unit;
