# RNNoise (Xiph.Org / Mozilla), BSD-3-Clause, see COPYING.
# Vendored from https://github.com/xiph/rnnoise tag v0.1.1 (6cbfd53e), built-in model (src/rnn_data.c).
# rnn_reader.c (loading custom models from files) is not vendored.
set(TARGET rnnoise)
add_library(${TARGET} STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/celt_lpc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/denoise.c
    ${CMAKE_CURRENT_LIST_DIR}/src/kiss_fft.c
    ${CMAKE_CURRENT_LIST_DIR}/src/pitch.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rnn.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rnn_data.c
)
set_target_properties(${TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(${TARGET} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_definitions(${TARGET} PRIVATE RNNOISE_BUILD)
if (NOT MSVC)
    target_compile_options(${TARGET} PRIVATE -w)
    target_link_libraries(${TARGET} PRIVATE m)
endif()
