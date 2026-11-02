# Unit tests (doctest). Run with: ctest --preset <debug|dev|release>
# Tests never use game data: anything disc-shaped is generated synthetically at test time.

add_executable(ac04_tests
    test_main.cpp
    test_command_line.cpp
    test_controls_file.cpp
    test_elf_loader.cpp
    test_folder_card.cpp
    test_frame_pacer.cpp
    test_gs.cpp
    test_libm.cpp
    test_guest_memory.cpp
    test_input.cpp
    test_iso9660.cpp
    test_output_layout.cpp
    test_pipeline_cache.cpp
    test_settings.cpp
    test_sha1.cpp
    test_skeleton.cpp
    test_pc_texts.cpp
    test_spu_mixer.cpp
    test_vectors_s4.cpp
    test_determinism.cpp
    test_leaks.cpp)
# ac04_platform: the binding parsers (test_input.cpp, test_controls_file.cpp) use SDL's key names.
target_link_libraries(ac04_tests PRIVATE ac04_game ac04_core ac04_platform ac04_doctest)
add_test(NAME ac04_tests COMMAND ac04_tests)
