#include "lux/factory_group.hpp"

#include "lux/board.hpp"
#include "lux/factory.hpp"
#include "lux/log.hpp"
#include "lux/mode.hpp"
using namespace std;


// Can assume only called on step idx 0
void FactoryGroup::finalize() {
    for (Factory *factory : board.player->factories()) {
        factory->new_action = factory->action;
    }
}

void FactoryGroup::do_build() {
    for (Factory *factory : board.player->factories()) {
        if (factory->last_action_step < this->step
            && factory->mode->do_build()) {
            factory->last_action_step = this->step;
        }
    }
}

void FactoryGroup::do_water() {
    for (Factory *factory : board.player->factories()) {
        if (factory->last_action_step < this->step
            && factory->mode->do_water()) {
            factory->last_action_step = this->step;
        }
    }
}

void FactoryGroup::do_none() {
    for (Factory *factory : board.player->factories()) {
        if (factory->last_action_step < this->step) {
            factory->do_none();
            factory->last_action_step = this->step;
        }
    }
}
