cmake_minimum_required(VERSION 3.16)
# The SDK can live under .pio-core inside a repository without its first commit.
# Do not let IDF's Git version probe accidentally inherit the application's Git.
set(PROJECT_VER "2.0.0")
if(WIN32)
    set(_git_ceiling_separator ";")
else()
    set(_git_ceiling_separator ":")
endif()
set(ENV{GIT_CEILING_DIRECTORIES} "${CMAKE_CURRENT_LIST_DIR}${_git_ceiling_separator}$ENV{GIT_CEILING_DIRECTORIES}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(auditor_medicacao)
