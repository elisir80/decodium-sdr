# Modulo QML DecodiumSdr (libreria) + eseguibile.
#
# Il modulo è una libreria e non parte dell'eseguibile perché i test Qt Quick
# devono poterlo importare senza tirarsi dietro main().

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/DecodiumSdr")
qt_add_library(dsdr_qml STATIC)

# I file QML stanno in qml/ per ordine, ma un tipo è visibile al modulo solo se
# sta nella radice: l'alias di risorsa riconcilia le due cose.
set(DSDR_QML_FILES
    qml/Main.qml
    qml/MainMenu.qml
    qml/CommandRail.qml
    qml/Theme.qml
    qml/Tuning.qml
    qml/DsdrButton.qml
    qml/DsdrSlider.qml
    qml/DsdrComboBox.qml
    qml/SignalMeter.qml
    qml/AnalogMeter.qml
    qml/SMeterScale.qml
    qml/DecoMeterS.qml
    qml/DecoMeterChip.qml
    qml/DecoMeter.qml
    qml/SMeterPanel.qml
    qml/ModeSelector.qml
    qml/FilterPresets.qml
    qml/PlateChip.qml
    qml/PlateToggle.qml
    qml/FrequencyDisplay.qml
    qml/BandPlan.qml
    qml/Memories.qml
    qml/Markers.qml
    qml/SpectrumCursor.qml
    qml/NetworkQuality.qml
    qml/AudioStudioPanel.qml
    qml/AudioScope.qml
    qml/EqCurve.qml
    qml/VoiceCompare.qml
    qml/GreylinePanel.qml
    qml/RotorDial.qml
    qml/AudioLevelBar.qml
    qml/ChainBlock.qml
    qml/ChainGlyph.qml
    qml/CfcBands.qml
    qml/ChainEndpoint.qml
    qml/ChainLink.qml
    qml/ChainFlowPanel.qml
    qml/PanelSwitchboard.qml
    qml/PanelWindow.qml
    qml/FrequencyPanel.qml
    qml/FrequencyGrid.qml
    qml/LevelScale.qml
    qml/TimeScale.qml
    qml/BandSegments.qml
    qml/VfoFlag.qml
    qml/VfoPlate.qml
    qml/PanadapterPane.qml
    qml/WaterfallControls.qml
    qml/TimeMachinePanel.qml
    qml/RxChainPanel.qml
    qml/NeuralNrControl.qml
    qml/TxPanel.qml
    qml/LevelBar.qml
    qml/ChannelStrip.qml
    qml/ChannelCard.qml
    qml/DiscoveryPane.qml
    qml/PanelFrame.qml
    qml/BackendPanelHost.qml
    qml/SoapyDevicePanel.qml
    qml/NetTcpDevicePanel.qml
    qml/ColibriDevicePanel.qml
    qml/AudiorigPanel.qml
    qml/IqFileDevicePanel.qml
)

foreach(qml_file IN LISTS DSDR_QML_FILES)
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/BandPlan.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Memories.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Markers.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Tuning.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/SMeterScale.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/FilterPresets.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        WaterfallPalette.h
        WaterfallPalette.cpp
        GreylineForeign.h
        RotorForeign.h
        GreylineMap.h
        GreylineMap.cpp
    # La scansione degli import serve solo per linkare staticamente i plugin
    # QML. Con un Qt dinamico non aggiunge nulla, e toglierla ci rende
    # indipendenti da qmlimportscanner, che in alcune distribuzioni (MSYS2)
    # non è funzionante. Andrà riattivata se si passerà a un Qt statico.
    NO_IMPORT_SCAN
)

# Qt 6.11 validates that a QML module's generated output directory ends in its
# URI path. Keeping the generated DecodiumSdr directory explicit also gives
# the deploy script stable plugin paths on macOS.
set_target_properties(dsdr_qml PROPERTIES
    QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/DecodiumSdr")

# §6.1: shader compilati con Qt Shader Tools, non a runtime.
qt_add_shaders(dsdr_qml "dsdr_panadapter_shaders"
    PREFIX "/shaders"
    BASE "shaders"
    FILES
        shaders/waterfall.vert
        shaders/waterfall.frag
        shaders/waterfall3d.vert
        shaders/waterfall3d.frag
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

# Su Windows: applicazione grafica, non di console.
#
# Senza questo l'eseguibile è di sottosistema «console» e Windows gli apre
# accanto una finestra nera che resta lì per tutta la sessione. Non serve a
# niente e sembra un errore — è la prima cosa che si nota lanciando il
# programma dal menù.
#
# Il log non si perde: `attachToParentConsole()` in main.cpp riaggancia i
# flussi standard alla console di chi ci ha lanciati, quando ce n'è una.
if(WIN32)
    set_target_properties(dsdr_app PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_compile_definitions(dsdr_app PRIVATE DSDR_VERSION="${PROJECT_VERSION}")

# Su macOS l'applicazione dev'essere un bundle, e non lo è per default: senza,
# il deploy di Qt si limita a copiare l'eseguibile e avvisa che «executable
# targets have to be app bundles». Il risultato è un binario che non parte su
# nessuna macchina che non abbia già Qt installato — e non c'è alcun .app da
# cui ricavare il DMG.
if(APPLE)
    set_target_properties(dsdr_app PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "DECODIUM SDR"
        MACOSX_BUNDLE_GUI_IDENTIFIER "it.decodium.sdr"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later"
    )
endif()

# ── Icona (packaging/icons/) ─────────────────────────────────────────────────
#
# I file dell'icona sono versionati e si rigenerano dall'SVG con
# `python packaging/icons/render.py`. Qui si limitano a entrare nei tre posti
# in cui ciascun sistema li cerca, che sono posti diversi:
#
#   Windows   una risorsa dentro l'eseguibile (l'esplora risorse legge quella)
#   macOS     un .icns dentro Contents/Resources, dichiarato nell'Info.plist
#   ovunque   `setWindowIcon`, che copre la finestra e la barra delle
#             applicazioni ma non il file su disco
#
# Nessuno dei tre sostituisce gli altri: senza il primo l'eseguibile Windows
# resta con l'icona generica per quanto bella sia quella della finestra.

set(DSDR_ICON_DIR "${CMAKE_SOURCE_DIR}/packaging/icons")

if(WIN32)
    set(DSDR_ICON_ICO "${CMAKE_SOURCE_DIR}/packaging/windows/decodium-sdr.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/windows/decodium-sdr.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/decodium-sdr.rc"
        @ONLY)
    target_sources(dsdr_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/decodium-sdr.rc")
elseif(APPLE)
    set(DSDR_ICON_ICNS "${CMAKE_SOURCE_DIR}/packaging/macos/decodium-sdr.icns")
    target_sources(dsdr_app PRIVATE "${DSDR_ICON_ICNS}")
    set_source_files_properties("${DSDR_ICON_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(dsdr_app PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "decodium-sdr.icns")
endif()

# Le coste del mondo, semplificate a un centesimo di grado: diciannove
# kilobyte di polilinee, di pubblico dominio (Natural Earth). Si rigenerano da
# `data/coste.py` — vedi il commento in testa a quel file.
qt_add_resources(dsdr_qml "dsdr_greyline_data"
    PREFIX "/data"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES "${CMAKE_SOURCE_DIR}/data/coste.bin")

qt_add_resources(dsdr_app "dsdr_appicon"
    PREFIX "/icons"
    BASE "${DSDR_ICON_DIR}"
    FILES "${DSDR_ICON_DIR}/it.decodium.sdr.png")

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

# Niente MACOS_BUNDLE_POST_BUILD: farebbe girare macdeployqt sul bundle appena
# linkato, dove i plugin QML non sono ancora stati installati, e riempirebbe il
# log di `install_name_tool: can't open file` senza far fallire nulla. Il
# deploy che conta è quello dell'install, sul bundle completo.
if(DSDR_QT_CMAKE_DEPLOY)
    # Fuori il backend multimediale di Qt. Di Qt Multimedia usiamo tre cose —
    # QAudioSink, QAudioSource, QMediaDevices — e vivono tutte nella libreria,
    # non nel plugin: il plugin serve a chi riproduce film, non a chi manda
    # campioni a una scheda audio. Provato togliendolo: gli otto dispositivi
    # di uscita e i cinque di ingresso si vedono come prima, e sia il sink sia
    # il source si aprono.
    #
    # Il plugin, però, è FFmpeg. Porta ventisette megabyte di librerie dentro
    # il pacchetto e ne pretende altre quarantadue che il deploy non raccoglie,
    # perché non sono dipendenze dell'eseguibile ma del plugin. Quelle
    # quarantadue sono la ragione per cui l'applicazione installata chiedeva
    # `libx264` all'avvio: qui non si vedeva, perché su una macchina che
    # compila il progetto stanno tutte nel PATH di MSYS2.
    set(DSDR_DEPLOY_PLUGIN_ARGS)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        list(APPEND DSDR_DEPLOY_PLUGIN_ARGS EXCLUDE_PLUGIN_TYPES multimedia)
    elseif(WIN32)
        # Prima della 6.10 la scelta dei plugin non passa dall'API CMake e
        # bisogna parlare allo strumento sottostante. Solo su Windows:
        # macdeployqt non conosce questa opzione, e il bundle macOS resta più
        # grosso del necessario ma completo, perche' li' le dipendenze del
        # plugin vengono risolte per davvero.
        list(APPEND DSDR_DEPLOY_PLUGIN_ARGS
             DEPLOY_TOOL_OPTIONS --skip-plugin-types multimedia)
    endif()

    qt_generate_deploy_qml_app_script(
        TARGET dsdr_app
        OUTPUT_SCRIPT DSDR_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
        ${DSDR_DEPLOY_PLUGIN_ARGS}
    )
    install(SCRIPT ${DSDR_DEPLOY_SCRIPT})
endif()

# SoapySDR discovers hardware drivers through shared modules at runtime. Qt's
# deploy tool can copy libSoapySDR because the application links it, but it
# cannot know which Soapy modules the user wants. The macOS release explicitly
# bundles the RTL-SDR module and its USB runtime so a downloaded DMG works
# without Homebrew.
if(APPLE AND DSDR_BACKEND_SOAPY AND DSDR_BUNDLE_RTLSDR)
    find_program(DSDR_BREW_EXECUTABLE brew)
    if(DSDR_BREW_EXECUTABLE)
        execute_process(
            COMMAND ${DSDR_BREW_EXECUTABLE} --prefix
            OUTPUT_VARIABLE DSDR_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    if(NOT DSDR_BREW_PREFIX)
        set(DSDR_BREW_PREFIX "/opt/homebrew")
    endif()

    set(DSDR_MACOS_ROOTS "${DSDR_BREW_PREFIX}" "/opt/homebrew" "/usr/local")
    list(REMOVE_DUPLICATES DSDR_MACOS_ROOTS)

    set(DSDR_SOAPY_MODULE_PATHS)
    set(DSDR_RTLSDR_LIBRARY_PATHS)
    set(DSDR_LIBUSB_LIBRARY_PATHS)
    foreach(dsdr_root IN LISTS DSDR_MACOS_ROOTS)
        list(APPEND DSDR_SOAPY_MODULE_PATHS
            "${dsdr_root}/lib/SoapySDR/modules0.8"
            "${dsdr_root}/opt/soapyrtlsdr/lib/SoapySDR/modules0.8")
        list(APPEND DSDR_RTLSDR_LIBRARY_PATHS
            "${dsdr_root}/opt/librtlsdr/lib"
            "${dsdr_root}/lib")
        list(APPEND DSDR_LIBUSB_LIBRARY_PATHS
            "${dsdr_root}/opt/libusb/lib"
            "${dsdr_root}/lib")
    endforeach()

    find_file(DSDR_SOAPY_RTLSDR_MODULE
        NAMES librtlsdrSupport.so
        PATHS ${DSDR_SOAPY_MODULE_PATHS}
        NO_DEFAULT_PATH)
    find_file(DSDR_LIBRTLSDR_LIBRARY
        NAMES librtlsdr.0.dylib
        PATHS ${DSDR_RTLSDR_LIBRARY_PATHS}
        NO_DEFAULT_PATH)
    find_file(DSDR_LIBUSB_LIBRARY
        NAMES libusb-1.0.0.dylib
        PATHS ${DSDR_LIBUSB_LIBRARY_PATHS}
        NO_DEFAULT_PATH)

    if(NOT DSDR_SOAPY_RTLSDR_MODULE OR NOT DSDR_LIBRTLSDR_LIBRARY OR NOT DSDR_LIBUSB_LIBRARY)
        message(FATAL_ERROR
            "DSDR_BUNDLE_RTLSDR=ON ma il runtime RTL-SDR non è completo.\n"
            "  macOS: brew install soapysdr soapyrtlsdr\n"
            "  modulo: ${DSDR_SOAPY_RTLSDR_MODULE}\n"
            "  librtlsdr: ${DSDR_LIBRTLSDR_LIBRARY}\n"
            "  libusb: ${DSDR_LIBUSB_LIBRARY}")
    endif()

    get_filename_component(DSDR_RTLSDR_BASENAME "${DSDR_LIBRTLSDR_LIBRARY}" NAME)
    get_filename_component(DSDR_LIBUSB_BASENAME "${DSDR_LIBUSB_LIBRARY}" NAME)
    set(DSDR_RTLSDR_SOURCE_NAME
        "${DSDR_BREW_PREFIX}/opt/librtlsdr/lib/${DSDR_RTLSDR_BASENAME}")
    set(DSDR_SOAPY_SOURCE_NAME
        "${DSDR_BREW_PREFIX}/opt/soapysdr/lib/libSoapySDR.0.8.dylib")
    set(DSDR_LIBUSB_SOURCE_NAME
        "${DSDR_BREW_PREFIX}/opt/libusb/lib/${DSDR_LIBUSB_BASENAME}")
    set(DSDR_LOADER_PATH "@loader_path")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/BundleMacOSSoapyRtlSdr.cmake.in"
        "${CMAKE_BINARY_DIR}/BundleMacOSSoapyRtlSdr.cmake"
        @ONLY)
    # When Qt deployment is disabled, the release workflow runs this script
    # after macdeployqt. macdeployqt must see a bundle without Soapy modules;
    # otherwise it tries to scan the third-party plugin as if it were QML.
    if(DSDR_QT_CMAKE_DEPLOY)
        install(SCRIPT "${CMAKE_BINARY_DIR}/BundleMacOSSoapyRtlSdr.cmake")
    endif()
endif()

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

        # Non solo l'eseguibile: anche le librerie e i plugin che il deploy di
        # Qt ha appena messo nel prefisso. Chiedere le dipendenze del solo
        # eseguibile lascia fuori quelle di secondo livello — quelle che un
        # plugin si porta dietro — e un plugin che non si carica non fa
        # fallire niente: fa sparire i JPEG, o l'audio, sulla macchina di
        # qualcun altro. Questo passo gira dopo l'install(SCRIPT) del deploy,
        # che e' dichiarato prima: quando arriviamo qui i plugin ci sono gia'.
        file(GLOB_RECURSE dsdr_installed_libs
             \"\${CMAKE_INSTALL_PREFIX}/bin/*.dll\"
             \"\${CMAKE_INSTALL_PREFIX}/share/qt6/plugins/*.dll\"
             \"\${CMAKE_INSTALL_PREFIX}/share/qt6/qml/*.dll\")
        list(LENGTH dsdr_installed_libs dsdr_lib_count)
        message(STATUS
            \"Raccolta delle dipendenze non-Qt: eseguibile + \${dsdr_lib_count} librerie\")

        # Il prefisso viene prima di tutto il resto. Un plugin sta in
        # share/qt6/plugins/<tipo>/ e le sue dipendenze non sono lì accanto:
        # senza questa riga le cerca nella directory del compilatore e trova
        # una seconda Qt6Core.dll, quella di MSYS2. CMake se ne accorge, dice
        # «multiple conflicting paths» e interrompe l'installazione a metà —
        # con un pacchetto che sembra costruito e non lo è.
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES \"\${dsdr_exe}\"
            MODULES \${dsdr_installed_libs}
            RESOLVED_DEPENDENCIES_VAR dsdr_resolved
            UNRESOLVED_DEPENDENCIES_VAR dsdr_unresolved
            DIRECTORIES \"\${CMAKE_INSTALL_PREFIX}/bin\" ${DSDR_DEP_DIRS}
            PRE_EXCLUDE_REGEXES \"api-ms-.*\" \"ext-ms-.*\"
            POST_EXCLUDE_REGEXES \".*[Ss]ystem32.*\" \".*[Ww]indows.*\"
        )

        foreach(dsdr_dep IN LISTS dsdr_resolved)
            # Quel che è già nel pacchetto ci resta: reinstallarlo su se stesso
            # non aggiunge niente e, se la sorgente è la copia già installata,
            # è una copia di un file su di sé.
            string(FIND \"\${dsdr_dep}\" \"\${CMAKE_INSTALL_PREFIX}\" dsdr_inside)
            if(dsdr_inside EQUAL 0)
                continue()
            endif()
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

# La libreria del ColibriNANO, se chi costruisce il pacchetto l'ha indicata.
# Va accanto all'eseguibile perché è lì che il backend la cerca per prima.
if(DSDR_COLIBRI_LIB)
    if(EXISTS "${DSDR_COLIBRI_LIB}")
        install(FILES "${DSDR_COLIBRI_LIB}" DESTINATION bin)
        message(STATUS "ColibriNANO: la libreria verrà inclusa nel pacchetto "
                       "(${DSDR_COLIBRI_LIB})")

        # E anche accanto all'eseguibile appena costruito: il backend la cerca
        # lì per prima, e senza questa copia il ColibriNANO funziona solo nei
        # pacchetti installati — mai durante lo sviluppo, che è quando ci si
        # attacca la radio davvero.
        add_custom_command(TARGET dsdr_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DSDR_COLIBRI_LIB}" "$<TARGET_FILE_DIR:dsdr_app>"
            COMMENT "ColibriNANO: libreria accanto all'eseguibile")
    else()
        # Meglio fermarsi in configurazione che scoprire il pacchetto monco
        # quando qualcuno ci attacca la radio.
        message(FATAL_ERROR
            "DSDR_COLIBRI_LIB punta a un file che non esiste: ${DSDR_COLIBRI_LIB}")
    endif()
endif()

qt_add_translations(dsdr_qml
    SOURCE_TARGETS dsdr_qml dsdr_core dsdr_hal
    TS_FILES ${DSDR_TS_FILES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none
)
