cmake_minimum_required(VERSION 3.13) 
project(stridegen)

########## Code generation

set(STRIDEGEN_SRC 
  src/codevalidator.cpp
  src/coderesolver.cpp
  src/codeanalysis.cpp
  src/strideframework.cpp
  src/stridesystem.cpp
  src/systemconfiguration.cpp
  src/languagesyntax.cpp
  src/toolmanager.cpp
  src/platformpolicymanager.cpp
)

set(STRIDEGEN_HEADERS
  include/stride/stridegen/builder.hpp
  include/stride/stridegen/codevalidator.hpp
  include/stride/stridegen/coderesolver.hpp
  include/stride/stridegen/codeanalysis.hpp
  include/stride/stridegen/strideframework.hpp
  include/stride/stridegen/stridesystem.hpp
  include/stride/stridegen/systemconfiguration.hpp
  include/stride/stridegen/languagesyntax.hpp
  include/stride/stridegen/codeentities.hpp
  include/stride/stridegen/toolmanager.hpp
  include/stride/stridegen/platformpolicymanager.hpp
)

#find_package(Qt5 5.7.0 REQUIRED COMPONENTS
#    Core
#)
if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    add_definitions(-DQ_OS_LINUX)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
    add_definitions(-DQ_OS_WINDOWS)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    add_definitions(-DQ_OS_MACOS)
endif()

#set(CMAKE_AUTOMOC ON)

add_library(StrideGen ${STRIDEGEN_SRC} ${STRIDEGEN_HEADERS})
add_library(StrideCodegen ALIAS StrideGen)

set_property(TARGET StrideGen PROPERTY POSITION_INDEPENDENT_CODE ON) #-fPIC
set_property(TARGET StrideGen PROPERTY CXX_STANDARD 20)
set_property(TARGET StrideGen PROPERTY CXX_STANDARD_REQUIRED ON)

add_subdirectory(tests)

if (STRIDE_CODE_COVERAGE)
  message("Building code coverage for StrideGen")
  set_target_properties(StrideGen PROPERTIES COMPILE_FLAGS "-g -O0 -Wall -fprofile-arcs -ftest-coverage")
  target_link_libraries(StrideGen gcov)
endif()

target_include_directories(StrideGen PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    target_link_libraries(StrideGen dl StrideParser StrideUtils)
else()
    target_link_libraries(StrideGen StrideParser StrideUtils)
endif()
