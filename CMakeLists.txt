cmake_minimum_required(VERSION 3.12) # minimum for C++ 20

project(lux-ai-agent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BINARY agent.out)

option(BUILD_DEBUG "Build in debug mode" OFF)
option(BUILD_WARNINGS "Build using all reasonable warnings" ON)

if(${BUILD_DEBUG})
    add_compile_definitions(
        DEBUG_BUILD
    )
endif()

if(${BUILD_WARNINGS})
    add_compile_options(
        -Wall
    )
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        add_compile_options(
            -W3
        )
    else()
        add_compile_options(
            -Werror
            -Wextra
            -Wpedantic
            -Wcast-align
            -Wunused
            -Wshadow
        )
    endif()
endif()

include(sources.cmake)

set(LUX_SRC_FILES
    src/lux/action.cpp
    src/lux/board.cpp
    src/lux/cell.cpp
    src/lux/defs.cpp
    src/lux/factory.cpp
    src/lux/factory_group.cpp
    src/lux/log.cpp
    src/lux/mode.cpp
    src/lux/mode_default.cpp
    src/lux/mode_ice_conflict.cpp
    src/lux/player.cpp
    src/lux/role.cpp
    src/lux/role_antagonizer.cpp
    src/lux/role_attacker.cpp
    src/lux/role_blockade.cpp
    src/lux/role_chain_transporter.cpp
    src/lux/role_cow.cpp
    src/lux/role_defender.cpp
    src/lux/role_miner.cpp
    src/lux/role_pillager.cpp
    src/lux/role_pincer.cpp
    src/lux/role_power_transporter.cpp
    src/lux/role_protector.cpp
    src/lux/role_recharge.cpp
    src/lux/role_relocate.cpp
    src/lux/role_water_transporter.cpp
    src/lux/team.cpp
    src/lux/unit.cpp
    src/lux/unit_group.cpp
)

add_executable(${BINARY} ${AGENT_SRC_FILES} ${LUX_SRC_FILES})

target_include_directories(${BINARY} PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
