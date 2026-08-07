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
    qml/ColibriDevicePanel.qml
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
# ── Installazione e deploy (RNF-04) ──────────────────────────────────────────
#
# `cmake --install build --prefix <dir>` produce un albero autosufficiente:
# l'eseguibile, le librerie Qt che servono e i moduli QML. Da lì i tre script
# in packaging/ ricavano AppImage, DMG e ZIP portable.

install(TARGETS dsdr_app
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

qt_generate_deploy_qml_app_script(
    TARGET dsdr_app
    OUTPUT_SCRIPT DSDR_DEPLOY_SCRIPT
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${DSDR_DEPLOY_SCRIPT})

# Il deploy di Qt porta con sé solo le librerie Qt. Restano fuori il runtime
# del compilatore, FFTW e SoapySDR: senza, il pacchetto non parte su una
# macchina che non abbia già l'ambiente di sviluppo installato.
if(WIN32)
    # Senza qt.conf l'applicazione non trova il plugin della piattaforma ed
    # esce in silenzio: il deploy usa un layout bin/ + share/, Qt su Windows
    # si aspetta i plugin accanto all'eseguibile.
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/windows/qt.conf DESTINATION bin)

    # L'ordine conta: le directory delle dipendenze esplicite vengono prima di
    # quella del compilatore, altrimenti una libreria che esiste in entrambe
    # verrebbe presa da quest'ultima — e se la versione di sistema non è
    # quella con cui abbiamo linkato, il pacchetto non parte.
    get_filename_component(DSDR_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(DSDR_DEP_DIRS)
    if(SoapySDR_DIR)
        get_filename_component(DSDR_SOAPY_ROOT "${SoapySDR_DIR}/.." ABSOLUTE)
        list(APPEND DSDR_DEP_DIRS "${DSDR_SOAPY_ROOT}/bin")
    endif()
    list(APPEND DSDR_DEP_DIRS "${DSDR_COMPILER_DIR}")

    install(CODE "
        set(dsdr_exe \"\${CMAKE_INSTALL_PREFIX}/bin/decodium-sdr.exe\")
        message(STATUS \"Raccolta delle dipendenze non-Qt di \${dsdr_exe}\")

        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \"\${dsdr_exe}\"
            RESOLVED_DEPENDENCIES_VAR dsdr_resolved
            UNRESOLVED_DEPENDENCIES_VAR dsdr_unresolved
            DIRECTORIES ${DSDR_DEP_DIRS}
            PRE_EXCLUDE_REGEXES \"api-ms-.*\" \"ext-ms-.*\"
            POST_EXCLUDE_REGEXES \".*[Ss]ystem32.*\" \".*[Ww]indows.*\"
        )

        foreach(dsdr_dep IN LISTS dsdr_resolved)
            file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\"
                 TYPE SHARED_LIBRARY FOLLOW_SYMLINK_CHAIN FILES \"\${dsdr_dep}\")
        endforeach()

        # Le non risolte sono quasi sempre DLL di sistema già escluse; se ne
        # resta qualcuna la si segnala, perché sarà il motivo per cui il
        # pacchetto non parte sulla macchina di qualcun altro.
        foreach(dsdr_missing IN LISTS dsdr_unresolved)
            message(WARNING \"dipendenza non risolta: \${dsdr_missing}\")
        endforeach()
    ")
endif()

qt_add_translations(dsdr_qml
    SOURCE_TARGETS dsdr_qml dsdr_core dsdr_hal
    TS_FILES ${DSDR_TS_FILES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none
)
