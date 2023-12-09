#pragma once

#include <utility>  // pair


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#define PDD  std::pair<double, double>
#define PII  std::pair<int, int>
#define PIII std::pair<PII, int>

// Lux-specific defs

#define SIZE 64
#define SIZE2 4096

#define CYCLE_LENGTH 50
#define DAY_LENGTH 30
#define UNIT_ACTION_QUEUE_SIZE 20

#define MAX_RUBBLE 100
#define FACTORY_RUBBLE_AFTER_DESTRUCTION 50
#define INIT_WATER_METAL_PER_FACTORY 150
#define INIT_POWER_PER_FACTORY 1000

#define MIN_LICHEN_TO_SPREAD 20
#define LICHEN_LOST_WITHOUT_WATER 1
#define LICHEN_GAINED_WITH_WATER 1
#define MAX_LICHEN_PER_TILE 100
#define POWER_PER_CONNECTED_LICHEN_TILE 1

#define LICHEN_WATERING_COST_FACTOR 10

#define FACTORY_PROCESSING_RATE_WATER 100
#define ICE_WATER_RATIO 4
#define FACTORY_PROCESSING_RATE_METAL 50
#define ORE_METAL_RATIO 5

#define FACTORY_CHARGE 50
#define FACTORY_WATER_CONSUMPTION 1

// Custom parameters

#define END_PHASE 875
#define ICE_RUSH_PHASE 970
#define FINAL_NIGHT_PHASE 980

#define NEVER_WATER_THRESHOLD 50
#define USUALLY_WATER_THRESHOLD 200
#define ALWAYS_WATER_THRESHOLD 300

#define MINER_SAFE_POWER_STEPS 100
#define MINER_LOW_POWER_STEPS 50

#define ICE_VULN_CHECK_RADIUS 8
#define ICE_VULN_DIST_THRESHOLD 5
#define ICE_VULN_COST_THRESHOLD 180

#define MIN_FACTORY_DIST 7

#define PROTECTOR_STRIKE_CHANCE 0.4

#define FUTURE_SIM_DEV 5
#define FUTURE_SIM_PROD 40

#define MAX_TIME_DEV 0.25
#define MAX_TIME_PROD 8.0

// Utility methods

double get_time();

bool prandom(int seed, double percent_chance);
int prandom_index(int seed, int len);
