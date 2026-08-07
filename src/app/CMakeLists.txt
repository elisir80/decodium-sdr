# Modulo QML DecodiumSdr (libreria) + eseguibile.
#
# Il modulo è una libreria e non parte dell'eseguibile perché i test Qt Quick
# devono poterlo importare senza tirarsi dietro main().

qt_add_library(dsdr_qml STATIC)

# I file QML stanno in qml/ per ordine, ma un tipo è visibile al modulo solo se
# sta nella radice: l'alias di risorsa riconcilia le due cose.
set(DSDR_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/DsdrButton.qml
    qml/DsdrSlider.qml
    qml/DsdrComboBox.qml
    qml/SignalMeter.qml
    qml/FrequencyGrid.qml
    qml/VfoFlag.qml
    qml/PanadapterPane.qml
    qml/ChannelStrip.qml
    qml/DiscoveryPane.qml
    qml/PanelFrame.qml
    qml/BackendPanelHost.qml
    qml/SoapyDevicePanel.qml
    qml/NetTcpDevicePanel.qml
)

foreach(qml_file IN LISTS DSDR_QML_FILES)
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(dsdr_qml
    URI DecodiumSdr
    VERSION 1.0
    QML_FILES ${DSDR_QML_FILES}
    # I tipi C++ esposti a QML vanno dichiarati qui: è da SOURCES che
    # qmltyperegistrar li raccoglie per generare le registrazioni.
    SOURCES
        SessionSingleton.h
        PanadapterView.h
        PanadapterView.cpp
        PanadapterRenderer.h
        PanadapterRenderer.cpp
    # La scansione degli import serve solo per linkare staticamente i plugin
    # QML. Con un Qt dinamico non aggiunge nulla, e toglierla ci rende
    # indipendenti da qmlimportscanner, che in alcune distribuzioni (MSYS2)
    # non è funzionante. Andrà riattivata se si passerà a un Qt statico.
    NO_IMPORT_SCAN
)

# §6.1: shader compilati con Qt Shader Tools, non a runtime.
qt_add_shaders(dsdr_qml "dsdr_panadapter_shaders"
    PREFIX "/shaders"
    BASE "shaders"
    FILES
        shaders/waterfall.vert
        shaders/waterfall.frag
        shaders/trace.vert
        shaders/trace.frag
)

target_link_libraries(dsdr_qml PUBLIC
    dsdr::core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
)

dsdr_link_qt_rhi(dsdr_qml)   # rhi/qrhi.h per il panadattatore
dsdr_apply_target_defaults(dsdr_qml)
add_library(dsdr::qml ALIAS dsdr_qml)

# ── Eseguibile ───────────────────────────────────────────────────────────────

qt_add_executable(dsdr_app main.cpp)

set_target_properties(dsdr_app PROPERTIES OUTPUT_NAME "decodium-sdr")
target_compile_definitions(dsdr_app PRIVATE DSDR_VERSION="${PROJECT_VERSION}")

target_link_libraries(dsdr_app PRIVATE dsdr_qml dsdr_qmlplugin)

dsdr_apply_target_defaults(dsdr_app)

# ── Traduzioni (RF-18) ───────────────────────────────────────────────────────
#
# L'italiano è la lingua sorgente delle stringhe e non ha un file .ts: le
# altre tredici lo hanno. La migrazione della lingua sorgente all'inglese è
# una decisione aperta, tracciata in ROADMAP.md, e non cambierebbe questa
# configurazione.
#
# `cmake --build build --target update_translations` riestrae le stringhe dai
# sorgenti; i .qm vengono compilati a ogni build e incorporati nelle risorse.

set(DSDR_LANGUAGES cs de en es fr ja lv nl pl pt ru uk zh_CN)

set(DSDR_TS_FILES)
foreach(lang IN LISTS DSDR_LANGUAGES)
    list(APPEND DSDR_TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/decodium_sdr_${lang}.ts")
endforeach()

# Le risorse vanno nella libreria del modulo QML, non nell'eseguibile: così le
# traduzioni sono disponibili anche ai test, che linkano la libreria ma non
# hanno un main() dell'applicazione.
qt_add_translations(dsdr_qml
    SOURCE_TARGETS dsdr_qml dsdr_core dsdr_hal
    TS_FILES ${DSDR_TS_FILES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none
)
