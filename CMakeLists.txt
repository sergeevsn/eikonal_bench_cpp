cmake_minimum_required(VERSION 3.14)
project(eikonal_bench_cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# --- thinks (Fast Marching Method) ---
FetchContent_Declare(
  thinks_fmm
  GIT_REPOSITORY https://github.com/thinks/fast-marching-method.git
  GIT_TAG        master
)
FetchContent_GetProperties(thinks_fmm)
if(NOT thinks_fmm_POPULATED)
    FetchContent_Populate(thinks_fmm)
endif()

# --- eikonalfm ---
FetchContent_Declare(
  eikonalfm_repo
  GIT_REPOSITORY https://github.com/kevinganster/eikonalfm.git
  GIT_TAG        main
)
FetchContent_GetProperties(eikonalfm_repo)
if(NOT eikonalfm_repo_POPULATED)
  FetchContent_Populate(eikonalfm_repo)
endif()

# --- matplotplusplus ---
FetchContent_Declare(
  matplotplusplus
  GIT_REPOSITORY https://github.com/alandefreitas/matplotplusplus.git
  GIT_TAG        origin/master
)
FetchContent_MakeAvailable(matplotplusplus)

# Main executable
add_executable(benchmark_cpp src/main.cpp)

# Include directories
target_include_directories(benchmark_cpp PRIVATE 
    include
    ${thinks_fmm_SOURCE_DIR}/include
    ${eikonalfm_repo_SOURCE_DIR}/src/eikonalfm
)

find_package(Threads REQUIRED)

# Link libraries
target_link_libraries(benchmark_cpp PRIVATE matplot Threads::Threads)

# eikonalfm sources (from fetched repo)
target_sources(benchmark_cpp PRIVATE
    ${eikonalfm_repo_SOURCE_DIR}/src/eikonalfm/marcher.cpp
    ${eikonalfm_repo_SOURCE_DIR}/src/eikonalfm/factoredmarcher.cpp
)

# Enable optimizations
target_compile_options(benchmark_cpp PRIVATE -O3 -march=native)
