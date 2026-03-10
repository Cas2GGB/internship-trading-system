cmake_minimum_required(VERSION 3.10)
project(MatchingEngine)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Compiler options
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-O3 -Wall -Wextra -pthread)
endif()

# Set output directory to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Include directories
# We include both include/common and include/engine so that #include "Header.h" etc. work directly.
include_directories(include/common include/engine)

# Define executable
add_executable(matching_engine 
    src/main.cpp 
    src/engine/Engine.cpp 
    src/engine/OrderBook.cpp
    src/engine/AccountManager.cpp
    src/engine/PriceLevel.cpp
)
