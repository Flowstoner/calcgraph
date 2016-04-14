cmake_minimum_required (VERSION 3.1.0 FATAL_ERROR)
project (calcgraph VERSION 1.0.0)

option(WITH_EXAMPLES "build the example applications " ON)
if(WITH_EXAMPLES)
    add_subdirectory(example)
endif(WITH_EXAMPLES)

add_subdirectory(test)

CONFIGURE_FILE(
  "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.pc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc"
  @ONLY
)

add_library(calcgraph INTERFACE)
target_include_directories(calcgraph INTERFACE "${PROJECT_SOURCE_DIR}/include")

install(FILES ${PROJECT_SOURCE_DIR}/include/calcgraph.h DESTINATION include)
install(FILES ${PROJECT_SOURCE_DIR}/calcgraph.pc DESTINATION lib/pkgconfig)
