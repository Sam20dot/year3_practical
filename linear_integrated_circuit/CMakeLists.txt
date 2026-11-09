cmake_minimum_required (VERSION 3.2)

project (year3_practical)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILER_COMMANDS ON)

add_executable(Unit2 
                      src/unit2/op_amp_basic.cpp
                      src/main.cpp)


