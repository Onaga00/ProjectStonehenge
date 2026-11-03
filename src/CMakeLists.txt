# ac04_core: code with no window/GPU dependency: guest memory model (src/common), paths, log,
#            settings, command line, disc image, ELF loader, pacing and layout logic.
#            Linked by the executable and the unit tests.
# ac04_platform: SDL3 window, input, audio and the Vulkan renderer.
# ac04_game: translated game code (src/game), the SDK layer (src/sce) and the IOP stand-ins. An
#            OBJECT library so the static AC04_GUEST_FUNCTION registrations are always linked.
# ProjectStonehenge: the executable.

add_library(ac04_core STATIC
    common/debug_hooks.cpp
    common/guest_call.cpp
    common/guest_memory.cpp
    common/guest_stack.cpp
    common/port_hooks.cpp
    platform/audio/spu_mixer.cpp
    platform/command_line.cpp
    platform/elf_loader.cpp
    platform/frame_pacer.cpp
    platform/iso9660.cpp
    platform/iso_image.cpp
    platform/message_text.cpp
    platform/log.cpp
    platform/paths.cpp
    platform/png_writer.cpp
    platform/settings.cpp
    platform/input_log.cpp
    platform/hash_log.cpp
    platform/p2m2_reader.cpp
    platform/sha1.cpp
    platform/memcard/folder_card.cpp
    platform/render/gs/gs_memory.cpp
    platform/render/gs/gs_state.cpp
    platform/render/output_layout.cpp
    platform/render/pipeline_cache.cpp
    platform/render/test_pattern.cpp
    platform/render/vu1/vif1.cpp
    platform/render/vu1/vu1_programs.cpp
    platform/vu0/vu0_hle.cpp
    platform/win32/crash_win32.cpp
    platform/win32/file_reader_win32.cpp
    platform/win32/paths_win32.cpp
    platform/win32/system_info_win32.cpp
    platform/win32/timer_win32.cpp)
target_link_libraries(ac04_core PUBLIC ac04_options ac04_json PRIVATE shell32 ole32 winmm)

add_library(ac04_platform STATIC
    platform/audio.cpp
    platform/controls_file.cpp
    platform/debug/overlay.cpp
    platform/input.cpp
    platform/window.cpp
    platform/render/gs/gs_vulkan.cpp
    platform/render/gs/gs_antialias.cpp
    platform/render/gpu_profiler.cpp
    platform/render/movie_upscaler.cpp
    platform/render/render_thread.cpp
    platform/render/renderer.cpp
    platform/render/frame_interpolator.cpp
    platform/render/gs_dump.cpp
    platform/render/vk_context.cpp
    platform/render/vma_impl.cpp)
target_link_libraries(ac04_platform PUBLIC ac04_core SDL3::SDL3-static ac04_volk ac04_vma ac04_imgui)
target_link_libraries(ac04_platform PRIVATE ac04_smaa_textures)
ac04_add_shaders(ac04_platform
    "${CMAKE_SOURCE_DIR}/shaders/output.vert"
    "${CMAKE_SOURCE_DIR}/shaders/output.frag"
    "${CMAKE_SOURCE_DIR}/shaders/gs.vert"
    "${CMAKE_SOURCE_DIR}/shaders/gs.frag"
    "${CMAKE_SOURCE_DIR}/shaders/gs_depth_copy.frag"
    "${CMAKE_SOURCE_DIR}/shaders/gs_conv.comp"
    "${CMAKE_SOURCE_DIR}/shaders/gs_decode.comp"
    "${CMAKE_SOURCE_DIR}/shaders/fsr1_easu.comp"
    "${CMAKE_SOURCE_DIR}/shaders/aa.vert"
    "${CMAKE_SOURCE_DIR}/shaders/smaa_edges.frag"
    "${CMAKE_SOURCE_DIR}/shaders/smaa_weights.frag"
    "${CMAKE_SOURCE_DIR}/shaders/smaa_blend.frag"
    "${CMAKE_SOURCE_DIR}/shaders/fxaa.frag")
# gs.frag without its second blend output, for the GS pipelines whose blend does not read it
# (docs/port/validation.md §2ar).
ac04_add_shader_variant(ac04_platform "${CMAKE_SOURCE_DIR}/shaders/gs.frag" gs_single_frag GS_SINGLE_SOURCE)

add_library(ac04_game OBJECT
    game/ai_aircraft.cpp
    game/ai_ground.cpp
    game/aircraft_data.cpp
    game/aircraft_models.cpp
    game/aircraft_viewer.cpp
    game/boot.cpp
    game/briefing_page.cpp
    game/camera.cpp
    game/campaign_menu_page.cpp
    game/campaign.cpp
    game/cd_file.cpp
    game/collision.cpp
    game/controller_page.cpp
    game/port_menu.cpp
    game/graphics_page.cpp
    game/quit_item.cpp
    game/pc_texts.cpp
    game/sky.cpp
    game/sound_page.cpp
    game/display.cpp
    game/effect_pools.cpp
    game/engine_sound.cpp
    game/flight_frame.cpp
    game/flight_load.cpp
    game/flight_sequences.cpp
    game/flight_systems.cpp
    game/flight_units.cpp
    game/hangar.cpp
    game/hdd.cpp
    game/hdd_install_page.cpp
    game/perf_counter.cpp
    game/interlude_page.cpp
    game/heap.cpp
    game/math.cpp
    game/menu.cpp
    game/mission_misc.cpp
    game/menu_object.cpp
    game/menu_widgets.cpp
    game/mission_select.cpp
    game/mission_title_page.cpp
    game/model.cpp
    game/movie.cpp
    game/movie_page.cpp
    game/message_window.cpp
    game/movie_init.cpp
    game/options.cpp
    game/options_page.cpp
    game/pad.cpp
    game/pending_stubs.cpp
    game/player_collision.cpp
    game/player_flight.cpp
    game/player_weapons.cpp
    game/projectile_draw.cpp
    game/projectiles.cpp
    game/effect_update.cpp
    game/effect_spawn.cpp
    game/hud.cpp
    game/mission_sound.cpp
    game/effect_draw.cpp
    game/radio.cpp
    game/radio_chatter.cpp
    game/random.cpp
    game/records_page.cpp
    game/replay.cpp
    game/replay_camera.cpp
    game/replay_record.cpp
    game/render2d.cpp
    game/results.cpp
    game/results_scene.cpp
    game/post_mission_page.cpp
    game/save_mc.cpp
    game/save_page.cpp
    game/save_init.cpp
    game/scene.cpp
    game/sequence.cpp
    game/sequence_model.cpp
    game/sound_rpc.cpp
    game/startup.cpp
    game/static_objects.cpp
    game/static_init.cpp
    game/terrain_query.cpp
    game/unit_models.cpp
    game/unit_states.cpp
    game/usbjoy.cpp
    game/vag_stream.cpp
    game/view_camera.cpp
    game/vu0_clip.cpp
    game/world_draw.cpp
    game/world_load.cpp
    platform/iop/iop_services.cpp
    sce/hw_registers.cpp
    sce/kernel.cpp
    sce/libc.cpp
    sce/libm.cpp
    sce/libcdvd.cpp
    sce/libgraph.cpp
    sce/libmc.cpp
    sce/libmpeg.cpp
    sce/libvu0.cpp
    sce/libpad.cpp
    sce/runtime.cpp
    sce/sce_host.cpp)
target_link_libraries(ac04_game PUBLIC ac04_core PRIVATE ac04_mpeg2)

add_executable(ProjectStonehenge main.cpp "${CMAKE_BINARY_DIR}/generated/version.rc")
target_link_libraries(ProjectStonehenge PRIVATE ac04_game ac04_platform)
# Release builds are a GUI program (no console window); development builds keep the console so
# the log is also visible there.
target_link_options(ProjectStonehenge PRIVATE $<IF:$<CONFIG:Release>,/SUBSYSTEM:WINDOWS,/SUBSYSTEM:CONSOLE>)
set_target_properties(ProjectStonehenge PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ProjectStonehenge)
