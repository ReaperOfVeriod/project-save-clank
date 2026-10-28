# Build with: cmake -S . -B build -G Ninja
# If OpenRAC is not checked out beside this project, pass -DOpenRACMod_DIR=...
cmake_minimum_required(VERSION 3.20)
project(project_save_clank LANGUAGES C)

if(NOT OpenRACMod_DIR)
  set(OpenRACMod_DIR "${CMAKE_CURRENT_LIST_DIR}/../OpenRAC/sdk/cmake")
endif()
find_package(OpenRACMod 1.5 REQUIRED)

openrac_add_mod(project_save_clank MANIFEST openrac-mod.json SOURCES src/project_save_clank.c)
