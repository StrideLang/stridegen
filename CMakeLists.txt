cmake_minimum_required(VERSION 3.13)
project(stridegen)

# Core Stride resolution, validation, analysis, hardware & platform management
set(STRIDEGEN_SRC
  src/codevalidator.cpp
  src/coderesolver.cpp
  src/codeanalysis.cpp
  src/entityusagetree.cpp
  src/strideframework.cpp
  src/stridesystem.cpp
  src/systemconfiguration.cpp
  src/toolmanager.cpp
  src/platformpolicymanager.cpp
  src/hardwareresolver.cpp
  src/passes/astnormalizer.cpp
  src/passes/connectionanddependencyanalyzer.cpp
  src/passes/domainresolver.cpp
  src/passes/gpioconfigurator.cpp
  src/passes/platforminitializer.cpp
  src/passes/policyvariantresolver.cpp
  src/passes/rateandtyperesolver.cpp
  src/passes/streamexpander.cpp
  src/passes/typecastresolver.cpp
)

set(STRIDEGEN_HEADERS
  include/stride/stridegen/codevalidator.hpp
  include/stride/stridegen/coderesolver.hpp
  include/stride/stridegen/codeanalysis.hpp
  include/stride/stridegen/strideframework.hpp
  include/stride/stridegen/stridesystem.hpp
  include/stride/stridegen/systemconfiguration.hpp
  include/stride/stridegen/toolmanager.hpp
  include/stride/stridegen/platformpolicymanager.hpp
  include/stride/stridegen/hardwareresolver.hpp
)

if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    add_definitions(-DQ_OS_LINUX)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
    add_definitions(-DQ_OS_WINDOWS)
elseif("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    add_definitions(-DQ_OS_MACOS)
endif()

add_library(StrideGen ${STRIDEGEN_SRC} ${STRIDEGEN_HEADERS})
add_library(StrideCodegen ALIAS StrideGen)

set_property(TARGET StrideGen PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET StrideGen PROPERTY CXX_STANDARD 20)
set_property(TARGET StrideGen PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(StrideGen PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(StrideGen PUBLIC StrideParser StrideUtils)

# Legacy C++ code generation backend (separated for future standalone extraction)
set(STRIDECPPGEN_SRC
  src/cppcodegen/domainconnectionbuilder.cpp
  src/cppcodegen/languagesyntax.cpp
  src/cppcodegen/signalaccess.cpp
)

set(STRIDECPPGEN_HEADERS
  include/stride/stridegen/cppcodegen/codeentities.hpp
  include/stride/stridegen/cppcodegen/domainconnectionbuilder.hpp
  include/stride/stridegen/cppcodegen/languagesyntax.hpp
  include/stride/stridegen/cppcodegen/signalaccess.hpp
)

add_library(StrideCppGen ${STRIDECPPGEN_SRC} ${STRIDECPPGEN_HEADERS})
set_property(TARGET StrideCppGen PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET StrideCppGen PROPERTY CXX_STANDARD 20)
set_property(TARGET StrideCppGen PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(StrideCppGen PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(StrideCppGen PUBLIC StrideGen)

# External builder plugin interface and dynamic loader
set(STRIDEPLUGINS_SRC
  src/plugins/pluginloader.cpp
)

set(STRIDEPLUGINS_HEADERS
  include/stride/stridegen/plugins/builder.hpp
  include/stride/stridegen/plugins/pluginloader.hpp
)

add_library(StridePlugins ${STRIDEPLUGINS_SRC} ${STRIDEPLUGINS_HEADERS})
set_property(TARGET StridePlugins PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET StridePlugins PROPERTY CXX_STANDARD 20)
set_property(TARGET StridePlugins PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(StridePlugins PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    target_link_libraries(StridePlugins PUBLIC StrideGen dl)
else()
    target_link_libraries(StridePlugins PUBLIC StrideGen)
endif()

add_subdirectory(tests)

if (STRIDE_CODE_COVERAGE)
  message("Building code coverage for StrideGen")
  set_target_properties(StrideGen StrideCppGen StridePlugins PROPERTIES COMPILE_FLAGS "-g -O0 -Wall -fprofile-arcs -ftest-coverage")
  target_link_libraries(StrideGen PUBLIC gcov)
endif()
