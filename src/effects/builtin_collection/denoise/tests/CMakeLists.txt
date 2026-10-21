#
# Audacity: A Digital Audio Editor
#
set(MODULE_TEST denoise_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/denoise_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../denoisecore.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../denoisecore.h
)

set(MODULE_TEST_LINK
    rnnoise
)

include(SetupGTest)
