# Replays audio through the tuner pipeline; see the comment at the top of
# pitch_test.cpp for usage.
add_executable(pitch_test pitch_test.cpp)
target_link_libraries(pitch_test PRIVATE spectro_dsp spectro_warnings)

add_test(NAME pitch_synthetic COMMAND pitch_test --synth)
