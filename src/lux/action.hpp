#pragma once

#include "lux/json.hpp"

#include <cstdint>
#include <iostream>


typedef enum FactoryAction : int8_t {
    FactoryAction_BUILD_LIGHT = 0,
    FactoryAction_BUILD_HEAVY,
    FactoryAction_WATER,
    FactoryAction_NONE,
} FactoryAction;

typedef enum UnitAction : int8_t {
    UnitAction_MOVE = 0,
    UnitAction_TRANSFER,
    UnitAction_PICKUP,
    UnitAction_DIG,
    UnitAction_SELF_DESTRUCT,
    UnitAction_RECHARGE,
} UnitAction;
static char UnitActionStr[] = { 'M', 'T', 'P', 'D', 'S', 'R' };

typedef enum Direction : int8_t {
    Direction_BEGIN = 0,
    Direction_CENTER = 0,
    Direction_NORTH = 1,
    Direction_EAST = 2,
    Direction_SOUTH = 3,
    Direction_WEST = 4,
    Direction_END = 5,
} Direction;
static char DirectionStr[] = { 'C', 'N', 'E', 'S', 'W' };

typedef enum Resource : int8_t {
    Resource_ICE = 0,
    Resource_ORE,
    Resource_WATER,
    Resource_METAL,
    Resource_POWER,
} Resource;
static char ResourceStr[] = { 'I', 'O', 'W', 'M', 'P' };

typedef struct ActionSpec {
    UnitAction action;
    Direction direction;
    Resource resource;
    int16_t amount;
    int16_t repeat;
    int16_t n;

    // ~~~ Methods:

    bool equal(struct ActionSpec *other);
    bool is_idle();

    friend std::ostream &operator<<(std::ostream &os, const struct ActionSpec &a) {
	os << UnitActionStr[a.action] << ','
           << ((a.action == UnitAction_MOVE || a.action == UnitAction_TRANSFER)
               ? DirectionStr[a.direction] : '_') << ','
           << ((a.action == UnitAction_TRANSFER || a.action == UnitAction_PICKUP)
               ? ResourceStr[a.resource] : '_') << ','
           << a.amount << ','
           << a.repeat << ','
           << a.n; return os;
    }
} ActionSpec;
void to_json(json& j, const ActionSpec& spec);

int direction_x(Direction direction);
int direction_y(Direction direction);
