# choppage-core: the whole of CHOPPAGE that isn't a host. Plain C++17: no JUCE, no OS calls on the audio path,
# no allocation or locks on the audio thread. Built into the desktop plugin, the MPC plugin and the tests.
add_library(choppage-core STATIC
    src/Akaizer.cpp
    src/Analysis.cpp
    src/RexFile.cpp
    src/AudioFile.cpp
    src/Engine.cpp
    src/Generator.cpp
    src/Params.cpp
    src/PerfFx.cpp
    src/Session.cpp
    src/SliceVoice.cpp
    src/SoundChain.cpp
    src/TransientDetector.cpp)
target_include_directories(choppage-core PUBLIC include)
target_compile_features(choppage-core PUBLIC cxx_std_17)
set_target_properties(choppage-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(choppage-core PUBLIC Threads::Threads)
if(NOT MSVC)
    target_compile_options(choppage-core PRIVATE -Wall -Wextra)
endif()
