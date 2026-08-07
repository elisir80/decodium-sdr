# Test Qt Quick sui componenti QML che contengono logica.

qt_add_executable(tst_qml_components tst_qml_components.cpp)

target_link_libraries(tst_qml_components PRIVATE
    Qt6::QuickTest
    Qt6::Qml
    dsdr::qml
    dsdr_qmlplugin
)

# QUICK_TEST_SOURCE_DIR dice al runner dove trovare i file tst_*.qml.
target_compile_definitions(tst_qml_components PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

dsdr_apply_target_defaults(tst_qml_components)

add_test(NAME tst_qml_components COMMAND tst_qml_components)
set_tests_properties(tst_qml_components PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)
