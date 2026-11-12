find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(media_library_qt WIN32 main.cpp)
qt_add_resources(media_library_qt frontend_icons PREFIX / FILES chevron-down.png)
target_link_libraries(media_library_qt PRIVATE media_business Qt6::Widgets)
target_compile_options(media_library_qt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(sample_csv "${PROJECT_SOURCE_DIR}/sample-data/data/media_records.csv")
set_property(TARGET media_library_qt APPEND PROPERTY LINK_DEPENDS "${sample_csv}")
add_custom_command(TARGET media_library_qt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sample_csv}"
            "$<TARGET_FILE_DIR:media_library_qt>/media_records_sample.csv"
    VERBATIM)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(media_frontend_tests tests/window_tests.cpp)
    set_target_properties(media_frontend_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(media_frontend_tests PRIVATE media_business Qt6::Widgets Qt6::Test)
    target_compile_definitions(media_frontend_tests PRIVATE
        SAMPLE_CSV="${sample_csv}"
        EXPANDED_CSV="${PROJECT_SOURCE_DIR}/sample-data/expanded/media_records_expanded.csv")
    target_compile_options(media_frontend_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    qt_add_resources(media_frontend_tests frontend_test_icons PREFIX / FILES chevron-down.png)
    add_test(NAME frontend_regressions COMMAND media_frontend_tests)
    set_tests_properties(frontend_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Explicit benchmark target: timings are evidence, not a hardware-dependent CTest gate.
    add_executable(media_frontend_benchmark EXCLUDE_FROM_ALL tests/window_benchmark.cpp)
    target_link_libraries(media_frontend_benchmark PRIVATE media_business Qt6::Widgets)
    target_compile_options(media_frontend_benchmark PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    qt_add_resources(media_frontend_benchmark frontend_benchmark_icons PREFIX / FILES chevron-down.png)
endif()
