# Include after care_algorithm_core, care_person, care_footprint and care_temporal.
add_library(care_person_snapshot STATIC "${CMAKE_CURRENT_LIST_DIR}/care_person_snapshot.cpp")
target_include_directories(care_person_snapshot
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(care_person_snapshot PUBLIC cxx_std_17)
target_link_libraries(care_person_snapshot PUBLIC care_algorithm_core Qt6::Core)
target_compile_options(care_person_snapshot PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(care_person_snapshot_tests "${CMAKE_CURRENT_LIST_DIR}/care_person_snapshot_tests.cpp")
    set_target_properties(care_person_snapshot_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(care_person_snapshot_tests PRIVATE
        care_person_snapshot care_person care_footprint care_temporal Qt6::Test)
    target_compile_definitions(care_person_snapshot_tests PRIVATE
        PERSON_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/../sample-data/person")
    target_compile_options(care_person_snapshot_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME care_person_snapshot COMMAND care_person_snapshot_tests)
endif()
