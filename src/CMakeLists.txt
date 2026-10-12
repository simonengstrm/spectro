# Signal processing shared by the app and the test harness, so a replayed file
# goes through exactly the code a live input does. Deliberately free of
# PortAudio and raylib.
add_library(spectro_dsp STATIC
    freq_analysis.cpp
    freq_analysis.h
    wav_io.cpp
    wav_io.h
)
target_include_directories(spectro_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spectro_dsp PRIVATE spectro_warnings)

add_executable(PitchDetector
    main.cpp
    audio_engine.cpp
    audio_engine.h
    gui.cpp
    gui.h
    recorder.cpp
    recorder.h
)
target_link_libraries(PitchDetector PRIVATE
    spectro_dsp
    spectro_warnings
    portaudio
    raylib
    imgui
)
if(WIN32)
    target_link_libraries(PitchDetector PRIVATE winmm)
endif()
