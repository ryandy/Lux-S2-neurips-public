#include "lux/log.hpp"

#include <fstream>

#include "lux/board.hpp"
using namespace std;


string log_prefix() {
    if (board.step == board.sim_step) {
        return to_string(board.step) + " ";
    } else {
        return to_string(board.step) + "@" + to_string(board.sim_step) + " ";
    }
}

namespace lux {
    void dumpJsonToFile(const char *path, json data) {
        std::fstream file(path, std::ios::trunc | std::ios::out);
        if (file.is_open()) {
            file << data;
            file.close();
        }
    }
}  // namespace lux
