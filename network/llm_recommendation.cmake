# Include from network/CMakeLists.txt after the shared media_search target exists.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_library(llm_recommendation STATIC "${CMAKE_CURRENT_LIST_DIR}/llm_recommendation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/llm_warehouse_adapter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/llm_candidate_recall.cpp")
target_include_directories(llm_recommendation PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(llm_recommendation PUBLIC cxx_std_17)
target_link_libraries(llm_recommendation PUBLIC media_search Qt6::Network)
target_compile_options(llm_recommendation PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Explicit, opt-in real API probe. Default invocation checks configuration/TLS only.
add_executable(llm_recommendation_smoke EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/llm_recommendation_smoke.cpp")
target_link_libraries(llm_recommendation_smoke PRIVATE llm_recommendation)
target_compile_options(llm_recommendation_smoke PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(llm_recommendation_tests "${CMAKE_CURRENT_LIST_DIR}/llm_recommendation_tests.cpp")
    set_target_properties(llm_recommendation_tests PROPERTIES AUTOMOC ON)
    target_compile_options(llm_recommendation_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(llm_recommendation_tests PRIVATE llm_recommendation Qt6::Test)
    add_test(NAME llm_recommendation COMMAND llm_recommendation_tests)
    # Fixture loading is optional and test-only; standalone consumers need only media_search and Qt.
    add_executable(llm_candidate_recall_tests "${CMAKE_CURRENT_LIST_DIR}/llm_candidate_recall_tests.cpp")
    set_target_properties(llm_candidate_recall_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(llm_candidate_recall_tests PRIVATE llm_recommendation Qt6::Test)
    if(TARGET media_business)
        target_link_libraries(llm_candidate_recall_tests PRIVATE media_business)
        target_compile_definitions(llm_candidate_recall_tests PRIVATE RECALL_CSV="${CMAKE_CURRENT_LIST_DIR}/../sample-data/data/media_records.csv")
    endif()
    target_compile_options(llm_candidate_recall_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME llm_candidate_recall COMMAND llm_candidate_recall_tests)
endif()
