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
    au3-module-manager
    au3-import-export
    au3-mixer
    au3-xml
    effects_builtin_collection

    muse::ui
    )

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)

# ---- minimal plugins for the plugin parameter automation tests ----------------------------
set(AUTOTEST_PLUGIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/testplugins)
set(AUTOTEST_PLUGIN_SRC ${CMAKE_CURRENT_LIST_DIR}/../../automation/tests/plugins)

if (AU_MODULE_EFFECTS_LV2)
    get_property(LV2SDK_PATH GLOBAL PROPERTY lv2sdk_SOURCE_DIR)
    if (NOT LV2SDK_PATH)
        set(LV2SDK_PATH ${CMAKE_BINARY_DIR}/_deps/lv2sdk)
    endif()
    add_library(autotest_lv2gain MODULE ${AUTOTEST_PLUGIN_SRC}/lv2gain/gain.c)
    target_include_directories(autotest_lv2gain PRIVATE ${LV2SDK_PATH}/lv2/include)
    target_link_libraries(autotest_lv2gain PRIVATE m)
    set_target_properties(autotest_lv2gain PROPERTIES
        PREFIX "" OUTPUT_NAME autogain
        LIBRARY_OUTPUT_DIRECTORY ${AUTOTEST_PLUGIN_DIR}/lv2/autogain.lv2)
    configure_file(${AUTOTEST_PLUGIN_SRC}/lv2gain/manifest.ttl ${AUTOTEST_PLUGIN_DIR}/lv2/autogain.lv2/manifest.ttl COPYONLY)
    configure_file(${AUTOTEST_PLUGIN_SRC}/lv2gain/autogain.ttl ${AUTOTEST_PLUGIN_DIR}/lv2/autogain.lv2/autogain.ttl COPYONLY)
    add_dependencies(${MODULE_TEST} autotest_lv2gain)
    target_compile_definitions(${MODULE_TEST} PRIVATE AUTOTEST_LV2)
    target_link_libraries(${MODULE_TEST} au3-lv2)
endif()

if (AU_MODULE_EFFECTS_VST AND OS_IS_LIN)
    get_property(VST3_SDK_PATH GLOBAL PROPERTY vst3sdk_SOURCE_DIR)
    set(S ${VST3_SDK_PATH})
    add_library(autotest_vst3gain MODULE
        ${AUTOTEST_PLUGIN_SRC}/vst3gain/vst3gain.cpp
        ${S}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
        ${S}/public.sdk/source/vst/vstbus.cpp
        ${S}/public.sdk/source/vst/vstcomponentbase.cpp
        ${S}/public.sdk/source/common/pluginview.cpp
        ${S}/public.sdk/source/common/commoniids.cpp
        ${S}/public.sdk/source/vst/vstparameters.cpp
        ${S}/public.sdk/source/vst/vstinitiids.cpp
        ${S}/public.sdk/source/main/pluginfactory.cpp
        ${S}/public.sdk/source/main/linuxmain.cpp
        ${S}/public.sdk/source/main/moduleinit.cpp
        ${S}/base/source/fobject.cpp
        ${S}/base/source/baseiids.cpp
        ${S}/base/source/fstring.cpp
        ${S}/base/source/fdebug.cpp
        ${S}/base/source/timer.cpp
        ${S}/base/source/updatehandler.cpp
        ${S}/base/thread/source/flock.cpp
        ${S}/pluginterfaces/base/funknown.cpp
        ${S}/pluginterfaces/base/coreiids.cpp
        ${S}/pluginterfaces/base/conststringtable.cpp
        ${S}/pluginterfaces/base/ustring.cpp
    )
    target_include_directories(autotest_vst3gain PRIVATE ${S})
    target_compile_definitions(autotest_vst3gain PRIVATE RELEASE=1)
    target_link_libraries(autotest_vst3gain PRIVATE pthread dl)
    set_target_properties(autotest_vst3gain PROPERTIES
        PREFIX "" OUTPUT_NAME AutoTestGain SUFFIX ".so"
        CXX_VISIBILITY_PRESET default
        LIBRARY_OUTPUT_DIRECTORY ${AUTOTEST_PLUGIN_DIR}/vst3/AutoTestGain.vst3/Contents/x86_64-linux)
    add_dependencies(${MODULE_TEST} autotest_vst3gain)
    target_compile_definitions(${MODULE_TEST} PRIVATE AUTOTEST_VST3)
    target_include_directories(${MODULE_TEST} PRIVATE ${S})
    target_link_libraries(${MODULE_TEST} au3-vst3)
endif()

target_compile_definitions(${MODULE_TEST} PRIVATE AUTOTEST_PLUGIN_DIR="${AUTOTEST_PLUGIN_DIR}")
