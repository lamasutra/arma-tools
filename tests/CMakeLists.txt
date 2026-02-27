include(GoogleTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

function(armatools_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE GTest::gtest_main)
    armatools_set_warnings(${name})
    gtest_discover_tests(${name})
endfunction()

# Per-library tests
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/binutil/test ${CMAKE_CURRENT_BINARY_DIR}/binutil_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/lzss/test ${CMAKE_CURRENT_BINARY_DIR}/lzss_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/lzo/test ${CMAKE_CURRENT_BINARY_DIR}/lzo_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/pbo/test ${CMAKE_CURRENT_BINARY_DIR}/pbo_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/heightpipe/test ${CMAKE_CURRENT_BINARY_DIR}/heightpipe_test)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/rvmat/test ${CMAKE_CURRENT_BINARY_DIR}/rvmat_test)

armatools_add_test(spec_validation_tests spec_validation_tests.cpp)
target_compile_definitions(spec_validation_tests PRIVATE ARMATOOLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

armatools_add_test(ir2mlod_roundtrip_tests ir2mlod_roundtrip_tests.cpp)
target_link_libraries(ir2mlod_roundtrip_tests PRIVATE armatools::p3d nlohmann_json::nlohmann_json)
add_dependencies(ir2mlod_roundtrip_tests ir2mlod mlod2ir)
target_compile_definitions(ir2mlod_roundtrip_tests PRIVATE
    ARMATOOLS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ARMATOOLS_BINARY_DIR="${CMAKE_BINARY_DIR}")

armatools_add_test(gui_tab_config_presenter_tests
    gui_tab_config_presenter_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/tab_config_presenter.cpp)
target_include_directories(gui_tab_config_presenter_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src
    ${CMAKE_SOURCE_DIR}/gui/src/core)

armatools_add_test(log_panel_presenter_tests
    log_panel_presenter_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/log_panel_presenter.cpp)
target_include_directories(log_panel_presenter_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(gl_model_camera_controller_tests
    gl_model_camera_controller_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/gl_model_camera_controller.cpp)
target_include_directories(gl_model_camera_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(rvmat_preview_camera_controller_tests
    rvmat_preview_camera_controller_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/rvmat_preview_camera_controller.cpp)
target_include_directories(rvmat_preview_camera_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(wrp_terrain_camera_controller_tests
    wrp_terrain_camera_controller_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/wrp_terrain_camera_controller.cpp)
target_include_directories(wrp_terrain_camera_controller_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(model_view_panel_presenter_tests
    model_view_panel_presenter_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/app/model_view_panel_presenter.cpp)
target_include_directories(model_view_panel_presenter_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src
    ${CMAKE_SOURCE_DIR}/libs/p3d/include)

armatools_add_test(render_domain_selection_tests
    render_domain_selection_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_backend_registry.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_backend_selection.cpp)
target_include_directories(render_domain_selection_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(ui_domain_selection_tests
    ui_domain_selection_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_backend_registry.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_backend_instance.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_backend_selection.cpp)
target_include_directories(ui_domain_selection_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(ui_domain_registry_tests
    ui_domain_registry_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_backend_registry.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_backend_instance.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_builtin_backends.cpp)
target_include_directories(ui_domain_registry_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(ui_domain_cli_override_tests
    ui_domain_cli_override_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_cli_override.cpp)
target_include_directories(ui_domain_cli_override_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(ui_runtime_config_tests
    ui_runtime_config_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_runtime_config.cpp)
target_include_directories(ui_runtime_config_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)
target_link_libraries(ui_runtime_config_tests PRIVATE
    nlohmann_json::nlohmann_json)

armatools_add_test(ui_event_adapter_tests
    ui_event_adapter_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/ui_domain/ui_event_adapter.cpp)
target_include_directories(ui_event_adapter_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(render_domain_scene_blob_builder_tests
    render_domain_scene_blob_builder_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_scene_blob.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_scene_blob_builder.cpp)
target_include_directories(render_domain_scene_blob_builder_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src
    ${CMAKE_SOURCE_DIR}/libs/p3d/include
    ${CMAKE_SOURCE_DIR}/libs/armapath/include)

armatools_add_test(render_domain_camera_blob_tests
    render_domain_camera_blob_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_scene_blob.cpp)
target_include_directories(render_domain_camera_blob_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)

armatools_add_test(render_domain_ui_bridge_tests
    render_domain_ui_bridge_tests.cpp
    ${CMAKE_SOURCE_DIR}/gui/src/render_domain/rd_ui_render_bridge.cpp)
target_include_directories(render_domain_ui_bridge_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/gui/src)
target_link_libraries(render_domain_ui_bridge_tests PRIVATE
    PkgConfig::EPOXY)
