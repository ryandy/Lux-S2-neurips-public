#include "lux/team.hpp"

#include "lux/board.hpp"
#include "lux/factory.hpp"
#include "lux/log.hpp"
#include "lux/unit.hpp"
using namespace std;


void Team::init(bool is_player0, json &strains_info) {
    this->id = (is_player0 ? 0 : 1);
    this->player.init(this, is_player0, strains_info);
}
