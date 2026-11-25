// The MPC edition's parameter list: the core parameters (choppage-core Params.h, shared with the desktop),
// hidden fillers up to a reserve, then the MPC-only controls. Indices are permanent once released: MPC projects
// store parameters by index. Add new MPC-only parameters at the end only.
#pragma once
#include "choppage/Params.h"
#include <string>
#include <vector>

namespace bc {

constexpr int kMpcReserveEnd = 96;     // core params live in [0, kNumCoreParams); fillers up to here
static_assert(kNumCoreParams <= kMpcReserveEnd, "the core parameter list outgrew the MPC reserve");

enum MpcParam : int
{
    MP_BREAK = kMpcReserveEnd,  // break 1-8 being edited / played (follows the break pads)
    MP_BROWSE,                  // file in Content/Breaks
    MP_LOAD,                    // trigger: load the browsed file into the break
    MP_DEMO,                    // trigger: load the demo break
    MP_AKAIZE,                  // trigger: Akaize the break to the MPC tempo (render into Content/Renders)
    MP_DICE_PAT,                // trigger: roll a pattern
    MP_DICE_FX,                 // trigger: roll the FX lane
    MP_UNDO,                    // trigger
    MP_REDO,                    // trigger
    MP_ORIGINAL,                // play the break as recorded
    MP_PLAY,                    // play on CHOPPAGE's own clock while the MPC is stopped
    MP_SLICE,                   // slice being edited, 1-64
    MP_EDIT_ALL,                // the slice controls edit every slice (ALL row)
    MP_SL_PITCH, MP_SL_GATE, MP_SL_STRETCH, MP_SL_REVERSE, MP_SL_STUTTER, MP_SL_TRANS, MP_SL_XFADE,
    MP_SL_MUTE,
    MP_SL_BADGE,                // K S G H O X
    MP_SLICING,                 // TRANSIENTS / GRID
    MP_SENS,                    // transient sensitivity
    MP_GRID,                    // 1/8 1/16 1/32
    MP_RESLICE,                 // trigger
    MP_LENGTH,                  // 1 / 2 / 4 bars
    MP_CLEAR,                   // trigger: clear the pattern
    MP_MEMORY,                  // 1-16
    MP_MEM_SAVE,                // trigger
    MP_MEM_RECALL,              // trigger
    MP_PAD_NOTES,               // which notes the 16 pads send
    // read-only readouts (their text belongs to the app; the value flips when the text changes so the screen redraws)
    MP_INFO_NAME, MP_INFO_BPM, MP_INFO_STATUS, MP_INFO_SLICE, MP_INFO_MSG,
    MP_PLAYHEAD,                // 0..1 through the pattern (drives the playhead bar on the LCD)
    MP_STEP0,                   // 64 step knobs: 0 = rest, k = slice k
    kMpcParamEnd = MP_STEP0 + 64
};
constexpr int kMpcNumParams = kMpcParamEnd;

enum class MpcKind { Float, Int, Bool, Choice };

struct MpcParamDef
{
    std::string id, name;
    MpcKind kind = MpcKind::Float;
    float min = 0, max = 1, def = 0;
    float skew = 1;                    // SDK skew: norm = lin^skew
    std::vector<std::string> choices;
    bool hidden = false;               // filler: not on any page, not automatable
    bool readout = false;              // written by the plugin only
    bool trigger = false;              // momentary: the app acts on 1 and puts it back to 0
};

const MpcParamDef& mpcParamDef(int index);
bool isStepParam(int index);
// The notes pad 1 plays for each MP_PAD_NOTES choice.
int padBaseNote(int choice);

} // namespace bc
