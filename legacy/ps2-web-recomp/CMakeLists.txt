cmake_minimum_required(VERSION 3.20)
project(ps2-web-recomp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PS2_GENERATED "" CACHE PATH "Folder with the C++ generated for a game")
option(PS2_BUILD_TESTS "Build native runtime regression tests" OFF)
option(PS2_NATIVE_X11 "Enable X11 window handles with SDL headers from a headless build" OFF)

if(EMSCRIPTEN)
    add_compile_options(-msimd128 -mtail-call -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -pthread)
    add_link_options(-msimd128 -mtail-call -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -pthread)
endif()

if(NOT EMSCRIPTEN)
    add_executable(eerecomp
        recomp/main.cpp
        recomp/r5900.cpp
        recomp/elf.cpp
        recomp/analysis.cpp
        recomp/codegen.cpp
        recomp/snr2.cpp)
    target_compile_options(eerecomp PRIVATE -Wall -Wextra)

    add_executable(vujit_dump recomp/vujit_dump.cpp runtime/src/vu_jit.cpp)
    target_include_directories(vujit_dump PRIVATE runtime/include runtime/src)
    add_executable(vurecomp recomp/vurecomp.cpp)
    target_include_directories(vurecomp PRIVATE runtime/include)
    target_compile_options(vurecomp PRIVATE -Wall -Wextra)
endif()

add_library(ps2runtime STATIC
    runtime/src/memory.cpp
    runtime/src/dispatch.cpp
    runtime/src/system.cpp
    runtime/src/kernel.cpp
    runtime/src/hw.cpp
    runtime/src/cop.cpp
    runtime/src/disc.cpp
    runtime/src/dmac.cpp
    runtime/src/vif.cpp
    runtime/src/vu_micro.cpp
    runtime/src/vu_jit.cpp
    runtime/src/vu_jit_web.cpp
    runtime/src/ipu/ipu.cpp
    runtime/src/gs/gs.cpp
    runtime/src/gs/gif.cpp
    runtime/src/gs/queue.cpp
    runtime/src/gs/raster.cpp
    runtime/src/iop/cpu.cpp
    runtime/src/iop/kernel.cpp
    runtime/src/iop/libs.cpp
    runtime/src/iop/hw.cpp
    runtime/src/iop/spu2.cpp
    runtime/src/iop/sio2.cpp
    runtime/src/iop/io.cpp
    runtime/src/iop/cdvd.cpp
    runtime/src/iop/sif.cpp
    runtime/src/iop/cdvdfsv.cpp)
target_include_directories(ps2runtime PUBLIC runtime/include)
target_compile_options(ps2runtime PRIVATE -Wall -Wextra)

set(PS2_WGPU_DIR "${CMAKE_SOURCE_DIR}/third_party/wgpu-native" CACHE PATH "wgpu-native release folder")
if(EMSCRIPTEN OR EXISTS "${PS2_WGPU_DIR}/lib/libwgpu_native.a")
    set(PS2_GS_WGSL "${CMAKE_BINARY_DIR}/gs_wgsl.cpp")
    add_custom_command(OUTPUT "${PS2_GS_WGSL}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu/gs.wgsl -DOUT=${PS2_GS_WGSL} -DNAME=kGsShader -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS runtime/src/gs/gpu/gs.wgsl cmake/embed.cmake)
    set(PS2_BLIT_WGSL "${CMAKE_BINARY_DIR}/blit_wgsl.cpp")
    add_custom_command(OUTPUT "${PS2_BLIT_WGSL}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu/blit.wgsl -DOUT=${PS2_BLIT_WGSL} -DNAME=kBlitShader -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS runtime/src/gs/gpu/blit.wgsl cmake/embed.cmake)
    set(PS2_POST_WGSL "${CMAKE_BINARY_DIR}/post_wgsl.cpp")
    add_custom_command(OUTPUT "${PS2_POST_WGSL}"
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu/post.wgsl -DOUT=${PS2_POST_WGSL} -DNAME=kPostShader -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS runtime/src/gs/gpu/post.wgsl cmake/embed.cmake)
    target_sources(ps2runtime PRIVATE runtime/src/gs/gpu/gpu.cpp "${PS2_GS_WGSL}" "${PS2_BLIT_WGSL}" "${PS2_POST_WGSL}")
    target_compile_definitions(ps2runtime PUBLIC PS2_GPU)
    if(EMSCRIPTEN)
        target_compile_options(ps2runtime PUBLIC --use-port=emdawnwebgpu)
        target_link_options(ps2runtime PUBLIC --use-port=emdawnwebgpu)
    else()
        target_include_directories(ps2runtime PRIVATE "${PS2_WGPU_DIR}/include")
        target_link_libraries(ps2runtime PUBLIC "${PS2_WGPU_DIR}/lib/libwgpu_native.a" dl pthread m)
        if(APPLE)
            target_link_libraries(ps2runtime PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework CoreFoundation" "-framework IOKit" "-framework IOSurface" objc)
        endif()
    endif()
endif()

if(PS2_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_executable(vu_flag_program tests/vu_flag_program.cpp runtime/src/vu_jit.cpp)
    target_include_directories(vu_flag_program PRIVATE runtime/include runtime/src)
    set(VU_FLAG_DIR "${CMAKE_CURRENT_BINARY_DIR}/vu-flag-fixture")
    add_custom_command(OUTPUT "${VU_FLAG_DIR}/generated/vu1_000.cpp" "${VU_FLAG_DIR}/generated/vu1_table.cpp"
        COMMAND vu_flag_program "${VU_FLAG_DIR}"
        COMMAND vurecomp "${VU_FLAG_DIR}/input" "${VU_FLAG_DIR}/generated"
        DEPENDS vu_flag_program vurecomp
        BYPRODUCTS "${VU_FLAG_DIR}/program.bin" "${VU_FLAG_DIR}/program.wasm" "${VU_FLAG_DIR}/layout.json")
    add_executable(vu_flag_or_test tests/vu_flag_or.cpp
        "${VU_FLAG_DIR}/generated/vu1_000.cpp" "${VU_FLAG_DIR}/generated/vu1_table.cpp")
    target_include_directories(vu_flag_or_test PRIVATE runtime/src)
    target_link_libraries(vu_flag_or_test PRIVATE ps2runtime)
    add_test(NAME vu_flag_or COMMAND vu_flag_or_test "${VU_FLAG_DIR}/program.bin")
    find_program(PS2_NODE_EXECUTABLE NAMES node nodejs)
    if(PS2_NODE_EXECUTABLE)
        add_test(NAME vu_flag_or_wasm COMMAND "${PS2_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/vu_flag_or.mjs" "${VU_FLAG_DIR}")
    endif()
    add_executable(kernel_return_test tests/kernel_return.cpp)
    target_include_directories(kernel_return_test PRIVATE runtime/src)
    target_link_libraries(kernel_return_test PRIVATE ps2runtime)
    add_test(NAME kernel_return COMMAND kernel_return_test)
    add_executable(kernel_state_test tests/kernel_state.cpp)
    target_include_directories(kernel_state_test PRIVATE runtime/src)
    target_link_libraries(kernel_state_test PRIVATE ps2runtime)
    add_test(NAME kernel_state COMMAND kernel_state_test)
    add_executable(cdvd_clock_test tests/cdvd_clock.cpp)
    target_include_directories(cdvd_clock_test PRIVATE runtime/src)
    target_link_libraries(cdvd_clock_test PRIVATE ps2runtime)
    add_test(NAME cdvd_clock COMMAND cdvd_clock_test)
    add_executable(input_script_test tests/input_script.cpp)
    target_include_directories(input_script_test PRIVATE runtime/src)
    target_link_libraries(input_script_test PRIVATE ps2runtime)
    add_test(NAME input_script COMMAND input_script_test)
    add_executable(gs_reset_test tests/gs_reset.cpp)
    target_include_directories(gs_reset_test PRIVATE runtime/src)
    target_link_libraries(gs_reset_test PRIVATE ps2runtime)
    add_test(NAME gs_reset COMMAND gs_reset_test)
    add_executable(memcard_flush_test tests/memcard_flush.cpp)
    target_include_directories(memcard_flush_test PRIVATE runtime/src)
    target_link_libraries(memcard_flush_test PRIVATE ps2runtime)
    add_test(NAME memcard_flush COMMAND memcard_flush_test)
    add_executable(memcard_format_test tests/memcard_format.cpp)
    target_include_directories(memcard_format_test PRIVATE runtime/src)
    target_link_libraries(memcard_format_test PRIVATE ps2runtime)
    add_test(NAME memcard_format COMMAND memcard_format_test)
    add_test(NAME play_wotm COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_play_wotm.sh")
    add_executable(gs_date_test tests/gs_date.cpp)
    target_include_directories(gs_date_test PRIVATE runtime/src)
    target_link_libraries(gs_date_test PRIVATE ps2runtime)
    add_test(NAME gs_date COMMAND gs_date_test)
    add_executable(vif_reset_test tests/vif_reset.cpp)
    target_include_directories(vif_reset_test PRIVATE runtime/src)
    target_link_libraries(vif_reset_test PRIVATE ps2runtime)
    add_test(NAME vif_reset COMMAND vif_reset_test)
    add_executable(gate_markers_test tests/gate_markers.cpp)
    target_include_directories(gate_markers_test PRIVATE runtime/src)
    target_link_libraries(gate_markers_test PRIVATE ps2runtime)
    add_test(NAME gate_markers COMMAND gate_markers_test)
    add_executable(spu2_volume_test tests/spu2_volume.cpp)
    target_include_directories(spu2_volume_test PRIVATE runtime/src)
    target_link_libraries(spu2_volume_test PRIVATE ps2runtime)
    add_test(NAME spu2_volume COMMAND spu2_volume_test)
    add_executable(spu2_mixer_test tests/spu2_mixer.cpp)
    target_include_directories(spu2_mixer_test PRIVATE runtime/src)
    target_link_libraries(spu2_mixer_test PRIVATE ps2runtime)
    add_test(NAME spu2_mixer COMMAND spu2_mixer_test)
    add_executable(ipu_macroblocks_test tests/ipu_macroblocks.cpp)
    target_include_directories(ipu_macroblocks_test PRIVATE runtime/src)
    target_link_libraries(ipu_macroblocks_test PRIVATE ps2runtime)
    add_test(NAME ipu_macroblocks COMMAND ipu_macroblocks_test)
    add_executable(vblank_wait_test tests/vblank_wait.cpp)
    target_include_directories(vblank_wait_test PRIVATE runtime/src runtime/include)
    target_link_libraries(vblank_wait_test PRIVATE ps2runtime)
    add_test(NAME vblank_wait COMMAND vblank_wait_test)
endif()

if(PS2_GENERATED)
    file(GLOB PS2_UNITS CONFIGURE_DEPENDS "${PS2_GENERATED}/*.cpp")
    add_library(ps2game STATIC ${PS2_UNITS})
    target_include_directories(ps2game PRIVATE "${PS2_GENERATED}")
    target_link_libraries(ps2game PUBLIC ps2runtime)
    target_compile_options(ps2game PRIVATE -Wno-unused-label -Wno-unused-variable)

    if(EMSCRIPTEN)
        set(PS2_WEB_ELF "" CACHE FILEPATH "Game executable preloaded as /game/core.elf")
        set(PS2_WEB_BOOT_FILE "" CACHE FILEPATH "Text file holding the argv[0] the game sees, preloaded as /game/boot.txt")
        set(PS2_WEB_PATCHES "" CACHE FILEPATH "Patch list the runtime writes into memory, preloaded as /game/patches.txt")
        add_executable(ps2web host/web.cpp)
        if(PS2_WEB_BOOT_FILE)
            target_link_options(ps2web PRIVATE "SHELL:--preload-file \"${PS2_WEB_BOOT_FILE}@/game/boot.txt\"")
        endif()
        if(PS2_WEB_PATCHES)
            target_link_options(ps2web PRIVATE "SHELL:--preload-file \"${PS2_WEB_PATCHES}@/game/patches.txt\"")
        endif()
        target_link_libraries(ps2web PRIVATE ps2game ps2runtime)
        target_link_options(ps2web PRIVATE --profiling-funcs -sJSPI -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sPTHREAD_POOL_SIZE=2 -sALLOW_TABLE_GROWTH=1 -lidbfs.js -sSTACK_SIZE=16777216 -sENVIRONMENT=web
            -sEXPORTED_FUNCTIONS=_main,_ps2web_pad,_ps2web_pad_connected,_ps2web_resize,_ps2web_gpu_ms,_ps2web_frames,_ps2web_primitives,_ps2web_audio_ring,_ps2web_audio_queued_ms,_ps2web_idle_ms,_ps2web_vu_interpreted
            -sEXPORTED_RUNTIME_METHODS=callMain,addRunDependency,removeRunDependency,HEAPU8,ENV
            --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html --preload-file ${PS2_WEB_ELF}@/game/core.elf)
        set_target_properties(ps2web PROPERTIES SUFFIX ".html" LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
        return()
    endif()

    add_executable(ps2run host/main.cpp)
    target_link_libraries(ps2run PRIVATE ps2game ps2runtime)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(ps2run PRIVATE host/profile.cpp)
        target_compile_definitions(ps2run PRIVATE PS2_PROFILE)
    endif()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND AND NOT EMSCRIPTEN)
        target_sources(ps2run PRIVATE host/window.cpp)
        target_compile_definitions(ps2run PRIVATE PS2_WINDOW)
        target_link_libraries(ps2run PRIVATE SDL2::SDL2)
        if(PS2_NATIVE_X11 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(ps2run PRIVATE SDL_VIDEO_DRIVER_X11=1)
        endif()
    endif()
endif()
