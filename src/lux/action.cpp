#include "lux/action.hpp"

#include "lux/exception.hpp"


bool ActionSpec::equal(ActionSpec *x) {
    if (x == NULL) return false;
    if (this->action != x->action) return false;

    switch (this->action) {
    case UnitAction_MOVE: return this->direction == x->direction;
    case UnitAction_TRANSFER: return (this->direction == x->direction
				      && this->resource == x->resource
				      && this->amount == x->amount);
    case UnitAction_PICKUP: return this->resource == x->resource && this->amount == x->amount;
    case UnitAction_DIG: return true;
    case UnitAction_RECHARGE: return this->amount == x->amount;
    case UnitAction_SELF_DESTRUCT: return true;
    default: LUX_ASSERT(false); return false;
    }
}

bool ActionSpec::is_idle() {
    return this->action == UnitAction_MOVE && this->direction == Direction_CENTER;
}

void to_json(json& j, const ActionSpec& spec) {
    j = json{spec.action, spec.direction, spec.resource, spec.amount, spec.repeat, spec.n};
}

int _direction_x_arr[] = {0, 0, 1, 0, -1};
int direction_x(Direction direction) {
    return _direction_x_arr[direction];
}

int _direction_y_arr[] = {0, -1, 0, 1, 0};
int direction_y(Direction direction) {
    return _direction_y_arr[direction];
}
