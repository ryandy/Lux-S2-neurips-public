#pragma once

#include <cstdint>
#include <string>

#include "lux/json.hpp"


typedef struct Agent {
    int64_t          step;
    std::string      player;
    int64_t          remainingOverageTime;
    int64_t          factories_per_team;
    int64_t          factories_left;

    int64_t water_left;
    int64_t metal_left;
    bool place_first;

    bool isTurnToPlaceFactory() const {
        return step % 2 == (place_first ? 1 : 0);
    }

    json setup();
    json act();
} Agent;
extern Agent agent;
