cmake_minimum_required(VERSION 2.8)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Built type: Debug Release RelWithDebInfo" FORCE)
endif()
message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})

find_package(Boost 1.61.0 REQUIRED COMPONENTS fiber)
include_directories(${Boost_INCLUDE_DIRS})

find_path(PTH_INCLUDE_DIR pth.h)
find_library(PTH_LIBRARY pth)
if(NOT PTH_INCLUDE_DIR OR NOT PTH_LIBRARY)
  message(FATAL_ERROR "pth library not found")
else()
  message(STATUS "Found pth: ${PTH_LIBRARY}")
endif()
set(PTH_INCLUDE_DIRS ${PTH_INCLUDE_DIR})
set(PTH_LIBRARIES ${PTH_LIBRARY})
include_directories(${PTH_INCLUDE_DIRS})

set(CMAKE_C_FLAGS "-I. -pthread -Wall")
set(CMAKE_C_FLAGS_DEBUG "-g -ggdb -D_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -g -ggdb -DNDEBUG")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

add_executable(a.out test.cpp coro.c)
target_link_libraries(a.out ${Boost_LIBRARIES} ${PTH_LIBRARIES})

#add_definitions(-DBOOST_FIBERS_NO_ATOMICS=1)
#add_definitions(-DCORO_UCONTEXT)
#add_definitions(-DCORO_LINUX)
#add_definitions(-DCORO_ASM)
