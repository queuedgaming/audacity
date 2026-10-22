#
# Audacity: A Digital Audio Editor
#

set(MODULE_TEST record_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp

    ${CMAKE_CURRENT_LIST_DIR}/../../au3audio/tests/mocks/audioenginemock.h
    ${CMAKE_CURRENT_LIST_DIR}/../../audio/tests/mocks/audiodrivercontrollermock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/recordconfigurationmock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/recordcontrollermock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/recordmock.h

    ${CMAKE_CURRENT_LIST_DIR}/recordcontroller_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/au3record_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/artistmodemodel_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/monitorprocessor_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/livemonitoringmodel_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/drumsynth.h
    )

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../../effects/builtin_collection
    )

set(MODULE_TEST_LINK
    record
    record_qml
    au3wrap
    au3-track
    au3-wave-track
    au3-project-history
    au3-project-file-io
    au3-numeric-formats
    au3-label-track
    au3-realtime-effects
    effects_builtin_collection

    muse::ui
    )

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)
