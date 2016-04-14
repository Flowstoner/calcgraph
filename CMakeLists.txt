cmake_minimum_required (VERSION 3.1.0 FATAL_ERROR)
project (calcgraph)

option(WITH_EXAMPLES "build the example applications " ON)
if(WITH_EXAMPLES)
    add_subdirectory(example)
endif(WITH_EXAMPLES)

add_subdirectory(test)

add_library(calcgraph INTERFACE)
target_include_directories(calcgraph INTERFACE "${PROJECT_SOURCE_DIR}/include")
install(FILES ${PROJECT_SOURCE_DIR}/include/calcgraph.h DESTINATION include)
