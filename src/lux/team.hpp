#pragma once

#include <cstdint>
#include <list>

#include "lux/player.hpp"


struct Unit;
struct Factory;

typedef struct Team {
    int8_t id;
    Player player;  // TODO allow for one or two

    // ~~~ Methods:

    void init(bool is_player0, json &strains_info);
} Team;
