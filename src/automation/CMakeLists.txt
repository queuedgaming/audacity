#
# Audacity: A Digital Audio Editor
#

declare_module(automation)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/automationmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/automationmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/automationtypes.h
    ${CMAKE_CURRENT_LIST_DIR}/iclipgaininteraction.h
    ${CMAKE_CURRENT_LIST_DIR}/iparamautomationinteraction.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3paramautomationinteraction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3paramautomationinteraction.h
    ${CMAKE_CURRENT_LIST_DIR}/view/paramautomationlanemodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/view/paramautomationlanemodel.h
    ${CMAKE_CURRENT_LIST_DIR}/ibusroutinginteraction.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3busroutinginteraction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3busroutinginteraction.h
    ${CMAKE_CURRENT_LIST_DIR}/view/busroutinggraphmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/view/busroutinggraphmodel.h

    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3clipgaininteraction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/au3/au3clipgaininteraction.h

    # view/ settings
    ${CMAKE_CURRENT_LIST_DIR}/view/clipgainmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/view/clipgainmodel.h
)

set(MODULE_USE_UNITY OFF)
set(MODULE_LINK
        au3wrap
        au3-mixer
        au3-wave-track
        au3-realtime-effects
        au3-components
)

setup_module()

if (AU_BUILD_AUTOMATION_TESTS)
    add_subdirectory(tests)
endif()
