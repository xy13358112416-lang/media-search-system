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
