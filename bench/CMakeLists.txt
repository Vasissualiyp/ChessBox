# SPDX-License-Identifier: GPL-3.0-or-later
file(GLOB CB_BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
if(CB_BENCH_SOURCES)
  add_executable(chessbox_bench ${CB_BENCH_SOURCES})
  target_link_libraries(chessbox_bench PRIVATE cb_flags ${CB_ALL_LAYER_TARGETS})
  target_include_directories(chessbox_bench PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()
