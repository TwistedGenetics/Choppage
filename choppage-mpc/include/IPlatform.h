// The thin layer between CHOPPAGE and a hardware host (the Twisted-Helix pattern). Everything CHOPPAGE needs from
// "the MPC" goes through this interface, so the MPC-specific code is one small adapter (src/ChoppageMpc.cpp, MPC
// Plugin SDK) and ChoppageMpcApp can be tested on a desktop with a fake platform (tests/test_mpc_app.cpp).
//
//   audio I/O   -> processReplacing, float, in place, 44.1 kHz, 32..512 frames per call
//   MIDI in     -> instrument MIDI with sample offsets (the pads, keys and sequencer)
//   file system -> the plugin folder's Content/ (mpc::contentFolder): Content/Breaks, Content/Renders
//   UI surface  -> declarative JSON skin: the plugin can only change parameter values and their text
#pragma once
#include <string>

namespace bc {

class IPlatform
{
public:
    virtual ~IPlatform() = default;
    virtual const char* platformName() const = 0;
    // <plugin folder>/Content on the MPC.
    virtual std::string contentDir() const = 0;
    // A parameter changed inside the plugin (mirrors, readouts, trigger reset): tell the host so the screen follows.
    // Never called from the audio thread.
    virtual void notifyParameter(int index, float plainValue) = 0;
};

} // namespace bc
