find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(warehouse_protocol STATIC media_json.cpp warehouse_protocol.cpp)
target_include_directories(warehouse_protocol PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(warehouse_protocol PUBLIC cxx_std_17)
target_link_libraries(warehouse_protocol PUBLIC Qt6::Core Qt6::Network)
target_compile_options(warehouse_protocol PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Shared care DTO validation only; no server implementation, database or paid API dependency.
add_library(care_contract STATIC care_contract.cpp)
target_include_directories(care_contract PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(care_contract PUBLIC cxx_std_17)
target_link_libraries(care_contract PUBLIC Qt6::Core)
target_compile_options(care_contract PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# PR 4 将其替换为包含 warehouse_client.cpp 的实际实现。
add_library(warehouse_client INTERFACE)
target_link_libraries(warehouse_client INTERFACE warehouse_protocol)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(warehouse_protocol_tests tests/protocol_tests.cpp)
    set_target_properties(warehouse_protocol_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(warehouse_protocol_tests PRIVATE warehouse_protocol Qt6::Test)
    target_compile_options(warehouse_protocol_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME warehouse_protocol_tests COMMAND warehouse_protocol_tests)
    add_executable(care_contract_tests tests/care_contract_tests.cpp)
    set_target_properties(care_contract_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(care_contract_tests PRIVATE care_contract warehouse_protocol Qt6::Test)
    target_compile_options(care_contract_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME care_contract_tests COMMAND care_contract_tests)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/llm_recommendation.cmake)
