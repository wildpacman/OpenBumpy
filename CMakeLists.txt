cmake_minimum_required(VERSION 3.25)
project(bumpy_port VERSION 0.1.0 LANGUAGES CXX)

# Opt-in instrumentation for comparing tick pacing between the desktop and web
# builds. Off by default: it prints to stdout every 300 waits.
option(BUMPY_PACE_PROBE "Print frame-pacing measurements" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten disables exception CATCHING by default: it compiles with
# -fignore-exceptions, which drops every `catch` handler, and turns a `throw` into an
# abort. This port uses exceptions for graceful degradation in five places that all
# matter to a player: a missing audio device leaves the game muted rather than dead,
# a machine or browser with no usable GL context falls back to the flat SDL_Renderer
# presentation, a shader failure disables the diorama for the run, a world whose
# assets fail to load keeps the player on the current one, and main()'s top-level
# catch reports the failure instead of vanishing. Without this flag every one of
# them aborts the tab instead of degrading.
#
# Directory scope, ahead of the dependencies, so it covers every translation unit an
# exception can unwind through; also a LINK option, because that is what makes emcc
# emit the invoke_* trampolines the catching depends on. -fexceptions (the JS-based
# implementation), not -fwasm-exceptions: the latter is smaller and faster but needs
# every object in the link, SDL included, to agree on it and rests on the Wasm EH
# proposal.
if(EMSCRIPTEN)
  add_compile_options(-fexceptions)
  add_link_options(-fexceptions)
endif()

include(FetchContent)
FetchContent_Declare(SDL
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG b8acae26b0453dad6250447ad32f6b0015b162ae)
FetchContent_Declare(ymfm
  GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
  GIT_TAG 17decfae857b92ab55fbb30ade2287ace095a381)
FetchContent_MakeAvailable(SDL Catch2 ymfm)

add_library(bumpy_core
  src/core/indexed_framebuffer.cpp
  src/core/asset_manifest.cpp
  src/core/sha256.cpp
  src/core/port_config.cpp
  src/game/menu.cpp
  src/game/high_scores.cpp
  src/game/high_score_screen.cpp
  src/game/password_screen.cpp
  src/game/app.cpp
  src/game/settings_overlay.cpp
  src/resources/binary_reader.cpp
  src/resources/vec.cpp
  src/resources/menu_resources.cpp
  src/resources/sprite_frame.cpp
  src/resources/font.cpp
  src/resources/adlib_bank.cpp
  src/resources/midi_song.cpp
  src/resources/sfx_tables.cpp
  src/resources/sfx_tables.gen.cpp
  src/resources/level_resources.cpp
  src/resources/world_resources.cpp
  src/resources/entity_sprites.cpp
  src/game/move_scripts.gen.cpp
  src/game/tile_reactions.cpp
  src/game/tile_reactions.gen.cpp
  src/game/ball_motion.cpp
  src/game/player_dispatch.gen.cpp
  src/game/object_anim.gen.cpp
  src/game/level_game.cpp
  src/video/menu_renderer.cpp
  src/video/screen_image.cpp
  src/video/board_renderer.cpp
  src/video/map_renderer.cpp
  src/video/screen_transition.cpp
  src/video/hud.cpp
  src/video/viewport.cpp
  src/video/high_score_renderer.cpp
  src/video/password_renderer.cpp
  src/video/settings_renderer.cpp
  src/game/world_map.cpp
  src/game/world_graphs.gen.cpp
  src/audio/opl2.cpp
  src/audio/midi_opl_player.cpp
  src/audio/speaker_sfx.cpp
  src/audio/audio_engine.cpp
  src/video3d/mat4.cpp
  src/video3d/blur.cpp
  src/video3d/scene3d.cpp
  src/video3d/slab_mesh.cpp)
target_include_directories(bumpy_core PUBLIC src)

target_sources(bumpy_core PRIVATE
  ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_ssg.cpp)
target_include_directories(bumpy_core PRIVATE ${ymfm_SOURCE_DIR}/src)

add_library(bumpy_platform_sdl3
  src/platform_sdl3/sdl_app.cpp
  src/platform_sdl3/sdl_audio.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3)
if(BUMPY_PACE_PROBE)
  target_compile_definitions(bumpy_platform_sdl3 PRIVATE BUMPY_PACE_PROBE)
endif()
# The GL layer builds for both platforms: desktop links opengl32 for the GL 1.1
# entry points, while Emscripten resolves everything through WebGL2 and needs no
# library. The sources themselves are identical -- they compile under emcc unchanged,
# because the port includes SDL's self-contained SDL_opengl_glext.h rather than the
# system GLES headers that lack the PFNGL*PROC typedefs.
target_sources(bumpy_platform_sdl3 PRIVATE
  src/platform_gl3/gl33.cpp
  src/platform_gl3/gl_util.cpp
  src/platform_gl3/gl_presenter.cpp
  src/platform_gl3/scene_renderer.cpp)
if(NOT EMSCRIPTEN)
  target_link_libraries(bumpy_platform_sdl3 PUBLIC opengl32)
endif()

add_executable(bumpy_port src/app/main.cpp)
target_link_libraries(bumpy_port PRIVATE bumpy_platform_sdl3)
# Windows exe icon: the Bumpy hero (src/app/bumpy.ico via src/app/bumpy_port.rc).
# The VS/MSVC generators compile .rc automatically.
if(WIN32)
  target_sources(bumpy_port PRIVATE src/app/bumpy_port.rc)
endif()
# Release is a GUI app: no console window (Windows GUI subsystem). SDL's SDL_main.h
# emits both main() and WinMain(), so the WINDOWS subsystem links WinMain -> SDL_main
# with no source change. Debug stays on the default CONSOLE subsystem so the dev CLI
# flags (--dump-music, --render-*, usage) keep a console for their stdout.
if(MSVC)
  target_link_options(bumpy_port PRIVATE $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>)
endif()
# Neither copy means anything for a wasm build: there is no SDL3 shared library to
# sit beside the executable, and the shaders reach the browser a different way --
# baked into bumpy.data by the staging rules above, not copied next to an exe.
if(NOT EMSCRIPTEN)
  add_custom_command(TARGET bumpy_port POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:SDL3::SDL3>
      $<TARGET_FILE_DIR:bumpy_port>)
  add_custom_command(TARGET bumpy_port POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/shaders3d
      $<TARGET_FILE_DIR:bumpy_port>/shaders3d)
endif()

# The offline tools and the test suite run natively, never in the browser.
if(NOT EMSCRIPTEN)
add_executable(audio_render tools/audio_render/main.cpp)
target_link_libraries(audio_render PRIVATE bumpy_core)

enable_testing()
add_executable(bumpy_tests
  tests/cpp/indexed_framebuffer_test.cpp
  tests/cpp/asset_manifest_test.cpp
  tests/cpp/port_config_test.cpp
  tests/cpp/sha256_test.cpp
  tests/cpp/binary_reader_test.cpp
  tests/cpp/vec_test.cpp
  tests/cpp/sprite_frame_test.cpp
  tests/cpp/font_test.cpp
  tests/cpp/adlib_bank_test.cpp
  tests/cpp/midi_song_test.cpp
  tests/cpp/sfx_tables_test.cpp
  tests/cpp/menu_resources_test.cpp
  tests/cpp/menu_renderer_test.cpp
  tests/cpp/level_resources_test.cpp
  tests/cpp/board_renderer_test.cpp
  tests/cpp/entity_sprites_test.cpp
  tests/cpp/menu_test.cpp
  tests/cpp/speed_pacer_test.cpp
  tests/cpp/password_screen_test.cpp
  tests/cpp/password_renderer_test.cpp
  tests/cpp/high_scores_test.cpp
  tests/cpp/high_score_screen_test.cpp
  tests/cpp/high_score_renderer_test.cpp
  tests/cpp/app_test.cpp
  tests/cpp/settings_overlay_test.cpp
  tests/cpp/settings_renderer_test.cpp
  tests/cpp/world_map_test.cpp
  tests/cpp/world_graphs_test.cpp
  tests/cpp/world_resources_test.cpp
  tests/cpp/screen_transition_test.cpp
  tests/cpp/map_renderer_test.cpp
  tests/cpp/hud_test.cpp
  tests/cpp/viewport_test.cpp
  tests/cpp/move_scripts_test.cpp
  tests/cpp/tile_reactions_test.cpp
  tests/cpp/ball_motion_test.cpp
  tests/cpp/level_game_test.cpp
  tests/cpp/opl2_test.cpp
  tests/cpp/midi_opl_player_test.cpp
  tests/cpp/speaker_sfx_test.cpp
  tests/cpp/audio_engine_test.cpp
  tests/cpp/mat4_test.cpp
  tests/cpp/blur_test.cpp
  tests/cpp/scene3d_test.cpp
  tests/cpp/slab_mesh_test.cpp)
target_link_libraries(bumpy_tests PRIVATE bumpy_core Catch2::Catch2WithMain)
add_test(NAME bumpy_tests COMMAND bumpy_tests)
set_tests_properties(bumpy_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()  # NOT EMSCRIPTEN (audio_render + tests)

if(EMSCRIPTEN)
  # Preload only what the game opens at runtime: 47 files, 586 KB. The manifest also
  # lists BUMPY.EXE, BUMP-Y.EXE and OLD-GAMES.NFO (~400 KB of DOS binaries and docs)
  # that nothing loads -- shipping them would nearly double every player's download.
  set(BUMPY_WEB_ASSETS
    BUMPRESE.VEC BUMPY.BNK BUMPY.MID BUMSPJEU.BIN DDFNT2.CAR DESSFIN.VEC
    FLECHE.BIN MASKBUMP.VEC QUELDISK SCORE.VEC TITRE.VEC)
  foreach(n RANGE 1 9)
    list(APPEND BUMPY_WEB_ASSETS D${n}.BUM D${n}.DEC D${n}.PAV MONDE${n}.VEC)
  endforeach()

  set(BUMPY_WEB_STAGE ${CMAKE_CURRENT_BINARY_DIR}/web-assets)
  set(BUMPY_WEB_STAGED "")
  foreach(asset IN LISTS BUMPY_WEB_ASSETS)
    add_custom_command(
      OUTPUT ${BUMPY_WEB_STAGE}/${asset}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/${asset} ${BUMPY_WEB_STAGE}/${asset}
      DEPENDS ${CMAKE_SOURCE_DIR}/${asset}
      COMMENT "staging web asset ${asset}")
    list(APPEND BUMPY_WEB_STAGED ${BUMPY_WEB_STAGE}/${asset})
  endforeach()
  add_custom_target(bumpy_web_assets DEPENDS ${BUMPY_WEB_STAGED})
  add_dependencies(bumpy_port bumpy_web_assets)

  # The diorama reads its GLSL at runtime, so the shaders ship in the asset image.
  # /assets/shaders3d is exactly where shader_dir() looks once SDL_GetBasePath()
  # yields nothing usable, which is the case in a browser.
  set(BUMPY_WEB_SHADERS scene.vert wall.frag sprite.frag shadow.frag bloom.frag)
  set(BUMPY_WEB_SHADERS_STAGED "")
  foreach(shader IN LISTS BUMPY_WEB_SHADERS)
    add_custom_command(
      OUTPUT ${BUMPY_WEB_STAGE}/shaders3d/${shader}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/shaders3d/${shader}
              ${BUMPY_WEB_STAGE}/shaders3d/${shader}
      DEPENDS ${CMAKE_SOURCE_DIR}/shaders3d/${shader}
      COMMENT "staging web shader ${shader}")
    list(APPEND BUMPY_WEB_SHADERS_STAGED ${BUMPY_WEB_STAGE}/shaders3d/${shader})
  endforeach()
  add_custom_target(bumpy_web_shaders DEPENDS ${BUMPY_WEB_SHADERS_STAGED})
  add_dependencies(bumpy_port bumpy_web_shaders)

  set_target_properties(bumpy_port PROPERTIES OUTPUT_NAME bumpy SUFFIX ".html")
  target_link_options(bumpy_port PRIVATE
    # The run loop still blocks; Asyncify is what turns emscripten_sleep into a real
    # yield so the browser gets its thread back (see src/platform_sdl3/sdl_app.cpp).
    -sASYNCIFY
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4194304
    # main() is held back until the shell's click-to-play gate, so the audio device
    # opens inside a user gesture.
    -sINVOKE_RUN=0
    -sEXPORTED_RUNTIME_METHODS=callMain
    # WebGL2 is GLES 3.0; without this Emscripten creates a WebGL1 context and every
    # GLES 3.0 entry point fails at runtime rather than at link time.
    -sMAX_WEBGL_VERSION=2
    # load_gl33 resolves all 39 post-1.1 entry points through SDL_GL_GetProcAddress,
    # which routes to emscripten_webgl_get_proc_address and is compiled out without this.
    -sGL_ENABLE_GET_PROC_ADDRESS
    --preload-file ${BUMPY_WEB_STAGE}@/assets
    --shell-file ${CMAKE_SOURCE_DIR}/src/web/shell.html)
  set_property(TARGET bumpy_port APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/web/shell.html)
endif()
