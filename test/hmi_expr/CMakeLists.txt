# Host-side test for hmi/HmiExpr (not part of the device build).
#
#   cmake -S test/hmi_expr -B build/hmi_expr_test -DCMAKE_PREFIX_PATH=~/Qt/6.9.1/gcc_64
#   cmake --build build/hmi_expr_test
#   build/hmi_expr_test/hmi_expr_test ../swr_web/referencs/hmi-testdata-convert.json \
#                                     ../swr_web/referencs/hmi-testdata-expr.json
cmake_minimum_required(VERSION 3.16)
project(hmi_expr_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(hmi_expr_test
  main.cpp
  ../../hmi/HmiExpr.h ../../hmi/HmiExpr.cpp
)
target_link_libraries(hmi_expr_test PRIVATE Qt6::Core)
