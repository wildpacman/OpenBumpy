cmake_minimum_required(VERSION 3.25)
project(bumpy_port VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/platform_sdl3/sdl_audio.cpp
  src/platform_gl3/gl33.cpp
  src/platform_gl3/gl_util.cpp
  src/platform_gl3/gl_presenter.cpp
  src/platform_gl3/scene_renderer.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3 opengl32)

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
add_custom_command(TARGET bumpy_port POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:bumpy_port>)
add_custom_command(TARGET bumpy_port POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders3d
    $<TARGET_FILE_DIR:bumpy_port>/shaders3d)

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
