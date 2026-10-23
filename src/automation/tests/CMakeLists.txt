#
# Audacity: A Digital Audio Editor
#

set(MODULE_TEST automation_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mocks/clipgaininteractionmock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/mixermocks.h
    ${CMAKE_CURRENT_LIST_DIR}/mixerpanelmodel_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/gainstagingmodel_tests.cpp
    )

set(MODULE_TEST_LINK
    automation
    au3-realtime-effects
    au3wrap
    wxBase
    projectscene
    muse::ui
    muse::uicomponents
    )

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)
