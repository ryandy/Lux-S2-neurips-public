#include <unistd.h>  // gethostname

#include <iostream>
#include <string>

#include "agent.hpp"
#include "lux/board.hpp"
#include "lux/exception.hpp"
#include "lux/json.hpp"
#include "lux/log.hpp"
using namespace std;


Agent agent;
Board board;
bool g_prod;


bool LUX_LOG_ON = true;
bool LUX_LOG_DEBUG_ON = false;
string LUX_LOG_PLAYER = "player_0";


bool is_prod() {
    string res = "";
    char tmp[0x100];
    if (gethostname(tmp, sizeof(tmp)) == 0) res = tmp;
    //return true;
    return res.rfind("Ryans", 0) != 0;
}


int _main() {
    g_prod = is_prod();

    while (std::cin && !std::cin.eof()) {
        LUX_LOG_DEBUG("main A");
        json input;
        std::cin >> input;
        //lux::dumpJsonToFile("input.json", input);

        input.at("step").get_to(agent.step);
        input.at("remainingOverageTime").get_to(agent.remainingOverageTime);
        if (agent.step == 0) {
            input.at("player").get_to(agent.player);
            input.at("obs").at("board").at("factories_per_team").get_to(agent.factories_per_team);
            input.at("obs").at("board").at("factories_per_team").get_to(agent.factories_left);
            LUX_LOG_ON = (g_prod || (agent.player == LUX_LOG_PLAYER));
        } else if (agent.step == 1) {
            input.at("obs").at("teams").at(agent.player).at("water").get_to(agent.water_left);
            input.at("obs").at("teams").at(agent.player).at("metal").get_to(agent.metal_left);
            input.at("obs").at("teams").at(agent.player).at("place_first").get_to(agent.place_first);
        }

        LUX_LOG_DEBUG("main B");
        static bool is_player0 = (agent.player == "player_0");
        board.init(input.at("obs"), agent.step, is_player0);

        LUX_LOG_DEBUG("main C");
        json output;
        if (board.real_env_step < 0) {
            output = agent.setup();
        } else {
            agent.step = board.real_env_step;
            output = agent.act();
        }

        LUX_LOG_DEBUG("main D " << output);
        //lux::dumpJsonToFile("last_actions.json", output);
        std::cout << output << std::endl;
    }
    return 0;
}

int main() {
    try {
        return _main();
    } catch (lux::Exception &e) {
        e.printStackTrace();
        throw;
    } catch (...) {
        LUX_LOG("Error: other exception v1");
        throw;
    }
}
