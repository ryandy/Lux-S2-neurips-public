#include "lux/mode_default.hpp"

#include "lux/action.hpp"
#include "lux/board.hpp"
#include "lux/exception.hpp"
#include "lux/unit.hpp"
using namespace std;


bool ModeDefault::from_factory(Mode **new_mode, Factory *_factory) {
    *new_mode = new ModeDefault(_factory);
    return true;
}

void ModeDefault::print(ostream& os) const {
    os << "Default";
}

void ModeDefault::set() {
    Mode::set();
}

void ModeDefault::unset() {
    if (this->is_set()) {
        Mode::unset();
    }
}

bool ModeDefault::is_valid() {
    return true;
}

Role *ModeDefault::get_transition_role(Unit *unit) {
    return this->_get_transition_role(unit);
}

Role *ModeDefault::get_new_role(Unit *unit) {
    return this->_get_new_role(unit);
}

bool ModeDefault::build_heavy_next() {
    return this->_build_heavy_next();
}

bool ModeDefault::do_build() {
    return this->_do_build();
}

bool ModeDefault::do_water() {
    return this->_do_water();
}

