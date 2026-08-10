# SPDX-License-Identifier: GPL-3.0-or-later
#
# Il programma di installazione, con CPack.
#
# CPack sta dentro CMake e non aggiunge dipendenze a chi compila: senza il
# generatore giusto installato produce comunque l'archivio, e il pacchetto
# vero lo fa chi ce l'ha. È la stessa regola delle opzioni di build — quella
# che esiste deve funzionare, e quella che non può funzionare non deve
# comparire.
#
# Su Windows il generatore è NSIS. Non è vendorizzato e non è richiesto: se
# `makensis` non c'è, `cpack` fa lo ZIP e lo dice.

set(CPACK_PACKAGE_NAME "DECODIUM SDR")
set(CPACK_PACKAGE_VENDOR "DECODIUM")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "DECODIUM <info@decodium.it>")

# La licenza la si legge installando, non dopo: è GPL-3.0, e chi installa ha
# diritto di saperlo prima.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# La cartella d'installazione porta il nome del prodotto senza versione: chi
# aggiorna non si ritrova due copie affiancate che si contendono le
# preferenze.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DECODIUM SDR")
set(CPACK_PACKAGE_FILE_NAME
    "decodium-sdr-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")

    set(CPACK_NSIS_PACKAGE_NAME "DECODIUM SDR")
    set(CPACK_NSIS_DISPLAY_NAME "DECODIUM SDR ${PROJECT_VERSION}")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_HELP_LINK "${PROJECT_HOMEPAGE_URL}")

    # L'icona dell'installatore e quella nel pannello «App installate»: la
    # stessa dell'eseguibile, o il programma sembrerebbe un altro a metà
    # dell'installazione.
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/packaging/windows/decodium-sdr.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/packaging/windows/decodium-sdr.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\decodium-sdr.exe")

    # Il collegamento nel menu Start e l'opzione per quello sul desktop.
    set(CPACK_PACKAGE_EXECUTABLES "decodium-sdr" "DECODIUM SDR")
    set(CPACK_CREATE_DESKTOP_LINKS "decodium-sdr")
    set(CPACK_NSIS_MODIFY_PATH OFF)

    # Disinstallando si toglie quello che si è messo, e nient'altro: le
    # preferenze e le registrazioni sono dell'operatore, non dell'installatore.
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    # Un avviso che vale la radio di qualcuno. Chi installa accanto a un
    # ricetrasmettitore deve saperlo prima, non dopo aver visto il PTT
    # scattare da solo.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        MessageBox MB_OK|MB_ICONINFORMATION \\\"Se colleghi un ricetrasmettitore via CAT: spegni 'CAT RTS' nei menu della radio.$\\\\r$\\\\n$\\\\r$\\\\nDECODIUM SDR comanda il PTT con un comando CAT e non usa quella linea; lasciandola attiva, qualunque programma che apra la porta seriale puo' mandare la radio in trasmissione per un istante.\\\"
    ")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "DECODIUM SDR")
else()
    # Su Linux l'AppImage lo fa il workflow di release, che sa fare il deploy
    # di Qt: qui restano gli archivi, utili a chi impacchetta per la propria
    # distribuzione.
    set(CPACK_GENERATOR "TGZ;ZIP")
endif()

# I sorgenti non si impacchettano da qui: per quelli c'è git, ed è più onesto.
set(CPACK_SOURCE_GENERATOR "")

include(CPack)
