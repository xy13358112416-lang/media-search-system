cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MediaRetrieval LANGUAGES CXX)
    include(CTest)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_library(media_search STATIC mediasearch.cpp mediarecommendation.cpp)
target_include_directories(media_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(media_search PUBLIC cxx_std_17)
target_link_libraries(media_search PUBLIC Qt6::Core)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(media_search_demo demo.cpp)
    target_link_libraries(media_search_demo PRIVATE media_search)
    add_executable(media_search_benchmark benchmark.cpp)
    target_link_libraries(media_search_benchmark PRIVATE media_search)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(media_search_tests mediasearch_tests.cpp)
    set_target_properties(media_search_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(media_search_tests PRIVATE media_search Qt6::Test)
    add_test(NAME search_scan COMMAND media_search_tests scanFindsExactMatches scanCombinesConditions)
    add_test(NAME search_index COMMAND media_search_tests fixedIndexMatchesScan)
    add_test(NAME search_updates COMMAND media_search_tests indexFollowsAddRemoveAndRebuild invalidUpdatesDoNotChangeResults)
    add_test(NAME search_optimization COMMAND media_search_tests shortestListReducesInitialCandidates missingConditionStopsEarly)

    add_executable(media_recommendation_tests mediarecommendation_tests.cpp
                   personalrecommendation_contract_compile.cpp)
    set_target_properties(media_recommendation_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(media_recommendation_tests PRIVATE media_search Qt6::Test)
    add_test(NAME recommendation COMMAND media_recommendation_tests)
endif()
