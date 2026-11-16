# Include after care_algorithm_core. Kept independent of existing retrieval APIs.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_library(care_footprint STATIC "${CMAKE_CURRENT_LIST_DIR}/care_footprint.cpp")
target_include_directories(care_footprint
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(care_footprint PUBLIC cxx_std_17)
target_compile_options(care_footprint PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(care_footprint PUBLIC care_algorithm_core Qt6::Core)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(care_footprint_tests "${CMAKE_CURRENT_LIST_DIR}/care_footprint_tests.cpp")
    set_target_properties(care_footprint_tests PROPERTIES AUTOMOC ON)
    target_compile_options(care_footprint_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(care_footprint_tests PRIVATE care_footprint Qt6::Test)
    add_test(NAME care_footprint COMMAND care_footprint_tests)
endif()
