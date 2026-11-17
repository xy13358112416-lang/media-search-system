find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(qt_online_startup STATIC online_startup.cpp)
target_include_directories(qt_online_startup PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qt_online_startup PUBLIC warehouse_client Qt6::Widgets)

add_library(qt_care_pages STATIC follow_center.cpp)
target_sources(qt_care_pages PRIVATE trending_page.cpp)
target_sources(qt_care_pages PRIVATE footprint_page.cpp)
target_sources(qt_care_pages PRIVATE temporal_recommendation_page.cpp participation_settings_dialog.cpp)
target_include_directories(qt_care_pages PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_features(qt_care_pages PUBLIC cxx_std_17)
target_link_libraries(qt_care_pages PUBLIC care_contract Qt6::Widgets)
target_compile_options(qt_care_pages PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_library(qt_online_window STATIC online_window.cpp)
target_include_directories(qt_online_window PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qt_online_window PUBLIC media_business warehouse_client qt_care_pages Qt6::Widgets)

add_executable(media_library_qt WIN32 main.cpp assistant_dialog.cpp warehouse_page.cpp)
qt_add_resources(media_library_qt frontend_icons PREFIX / FILES chevron-down.png)
target_link_libraries(media_library_qt PRIVATE media_business warehouse_client qt_care_pages qt_online_startup qt_online_window Qt6::Widgets)
target_compile_options(media_library_qt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

set(sample_csv "${PROJECT_SOURCE_DIR}/sample-data/data/media_records.csv")
set_property(TARGET media_library_qt APPEND PROPERTY LINK_DEPENDS "${sample_csv}")
add_custom_command(TARGET media_library_qt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${sample_csv}"
            "$<TARGET_FILE_DIR:media_library_qt>/media_records_sample.csv"
    VERBATIM)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(widget_test_environment "QT_QPA_PLATFORM=offscreen")
    if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
        # A deployed build directory can contain only the Windows plugin.
        list(APPEND widget_test_environment
            "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>")
    endif()
    add_executable(online_window_tests tests/online_window_tests.cpp warehouse_page.cpp assistant_dialog.cpp)
    set_target_properties(online_window_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(online_window_tests PRIVATE qt_online_window Qt6::Test)
    add_test(NAME online_window_tests COMMAND online_window_tests)
    add_executable(online_startup_tests tests/online_startup_tests.cpp)
    set_target_properties(online_startup_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(online_startup_tests PRIVATE qt_online_startup Qt6::Test)
    add_test(NAME online_startup_tests COMMAND online_startup_tests)
    add_executable(care_pages_tests tests/care_pages_tests.cpp)
    set_target_properties(care_pages_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(care_pages_tests PRIVATE qt_care_pages Qt6::Widgets Qt6::Test)
    target_compile_options(care_pages_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME care_pages_tests COMMAND care_pages_tests)
    add_executable(media_frontend_tests tests/window_tests.cpp assistant_dialog.cpp warehouse_page.cpp)
    set_target_properties(media_frontend_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(media_frontend_tests PRIVATE media_business warehouse_client qt_care_pages qt_online_startup qt_online_window Qt6::Widgets Qt6::Test)
    target_compile_definitions(media_frontend_tests PRIVATE
        REAL_CSV="${sample_csv}"
        SAMPLE_CSV="${PROJECT_SOURCE_DIR}/sample-data/tests/fixtures/media_records_synthetic.csv"
        EXPANDED_CSV="${PROJECT_SOURCE_DIR}/sample-data/tests/fixtures/media_records_expanded_legacy.csv")
    target_compile_options(media_frontend_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    qt_add_resources(media_frontend_tests frontend_test_icons PREFIX / FILES chevron-down.png)
    add_test(NAME frontend_regressions COMMAND media_frontend_tests)


    add_executable(warehouse_page_tests tests/warehouse_page_tests.cpp warehouse_page.cpp)
    set_target_properties(warehouse_page_tests PROPERTIES AUTOMOC ON)
    target_include_directories(warehouse_page_tests PRIVATE ${PROJECT_SOURCE_DIR}/server)
    target_link_libraries(warehouse_page_tests PRIVATE warehouse_client warehouse_server qt_care_pages Qt6::Widgets Qt6::Test)
    target_compile_options(warehouse_page_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_test(NAME warehouse_page_tests COMMAND warehouse_page_tests -o -,txt)
    set_tests_properties(online_window_tests online_startup_tests care_pages_tests
        frontend_regressions warehouse_page_tests PROPERTIES ENVIRONMENT "${widget_test_environment}")

    # Explicit benchmark target: timings are evidence, not a hardware-dependent CTest gate.
    add_executable(media_frontend_benchmark EXCLUDE_FROM_ALL tests/window_benchmark.cpp assistant_dialog.cpp warehouse_page.cpp)
    target_link_libraries(media_frontend_benchmark PRIVATE media_business warehouse_client qt_care_pages qt_online_startup qt_online_window Qt6::Widgets)
    target_compile_options(media_frontend_benchmark PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    qt_add_resources(media_frontend_benchmark frontend_benchmark_icons PREFIX / FILES chevron-down.png)
endif()
