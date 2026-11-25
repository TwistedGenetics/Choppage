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
    // ---- 0.3: the BR3AK main screen and the waveform (appended: indices above stay put)
    MP_VIEW = MP_STEP0 + 64,    // main page section: MAIN (FX pads + knobs) 0, FX 1, DISK 2, PATTERN 3 (app-written)
    MP_SHOW_PAT, MP_SHOW_FX, MP_SHOW_DISK,   // the PATTERN / FX bars and LOAD: tap to open that section, again to close
    MP_BRK_PREV, MP_BRK_NEXT,   // triggers: the LCD's arrows either side of the break name
    MP_SL_MOVE,                 // the selected slice's marker, 0..1 through the break
    MP_SL_NUDGE_L, MP_SL_NUDGE_R, MP_SL_ADD, MP_SL_DEL,   // triggers
    MP_ZOOM,                    // the waveform shows ALL of the break or the selected SLICE
    MP_WAVE_TAP,                // a tap on the waveform: column + 1 (springs back to 0)
    MP_LIST_TAP,                // a tap on a DISK list line: line + 1 (springs back to 0)
    MP_INFO_SLPARAMS,           // readout: the selected slice's settings in one line
    MP_LIST0,                   // readouts: 5 lines of the DISK list
    MP_WAVE0 = MP_LIST0 + 5,    // waveform columns (film strip frame, see waveFrame())
    MP_TAB0 = MP_WAVE0 + 84,    // the hit-letter row under the waveform (film strip frame, see tabFrame())
    // the BR3AK SEQ grid: 6 lanes (KICK SNARE GHOST HAT OPEN PERC) x 16 steps of the page shown
    MP_SEQ_PAGE = MP_TAB0 + 84, // steps 1-16 / 17-32 / 33-48 / 49-64
    MP_SEQ_FOLLOW,              // the page follows the playhead
    MP_SEQ_TAP,                 // a tap on the grid: lane * 16 + column + 1 (springs back to 0)
    MP_SEQ_HEAD,                // the step-number row: theme * 17 + (0 = not playing here, 1-16 = the step playing)
    MP_CELL0,                   // 96 grid cells (film strip frame, see cellFrame())
    MP_VSTEP0 = MP_CELL0 + 96,  // 16 step knobs for the page shown (the Q-Links on the SEQ screens)
    MP_INFO_BRK = MP_VSTEP0 + 16,   // readout: "BRK 3" (the SEQ page's corner)
    MP_SL_END,                  // the selected slice's end (= the next slice's start), 0..1 through the break
    MP_SEQ_SEL,                 // the step selected on the grid (1-64): STEP FX and RATE edit it
    MP_STEP_FX,                 // the selected step's FX: OFF STUTTER REVERSE STRETCH TAPE STOP
    MP_STEP_RATE,               // its rate: 1/8 1/16 1/32 1/64
    MP_FXROW0,                  // 16 FX row cells under the grid (film strip frame, see fxRowFrame())
    kMpcParamEnd = MP_FXROW0 + 16
};
constexpr int kMpcNumParams = kMpcParamEnd;

// The waveform on the MPC: kWaveCols picture columns, each a film strip whose frame the app picks.
// wave frame = ((theme * 3 + look) * 2 + mark) * kWaveLevels + level
//   theme: LCD colour 0-3, look: 0 plain / 1 selected slice / 2 slice playing now, mark: slice marker in this column,
//   level: 0 silence .. kWaveLevels-1 full scale.
// tab frame = theme * kTabFrames + t; t: 0 none, 1-6 a slice starts here (K S G H O X), 7-12 the same selected,
//   13 inside a slice, 14 inside the selected slice.
constexpr int kWaveCols = 84;
constexpr int kWaveLevels = 12;
constexpr int kWaveFrames = 4 * 3 * 2 * kWaveLevels;
constexpr int kTabFrames = 15;
constexpr int kListLines = 5;
inline int waveFrame(int theme, int look, int mark, int level) { return ((theme * 3 + look) * 2 + mark) * kWaveLevels + level; }
inline int tabFrame(int theme, int t) { return theme * kTabFrames + t; }

// SEQ grid (lanes KICK SNARE GHOST HAT OPEN PERC). Cell frame = 1 + theme * kCellFrames + c; c: 0 empty, 1 empty on
// a beat, 2-65 a hit playing slice c - 1. Frame 0 and the last frame are spare, so the jog wheel can always move a
// cell's value both ways (the app reads the direction, cycles the lane's slices, and puts the right frame back).
// FX row frame = theme * kFxRowFrames + (selected ? 17 : 0) + f; f: 0 none, 1-16 type 1-4 x rate 8 / 16 / 32 / 64.
// The step row: theme * 17 + step (0 = none).
constexpr int kSeqLanes = 6;
constexpr int kCellFrames = 66;
constexpr int kCellStripFrames = 4 * kCellFrames + 2;
constexpr int kFxRowFrames = 34;
inline int cellFrame(int theme, int c) { return 1 + theme * kCellFrames + c; }
inline int fxRowFrame(int theme, bool selected, int f) { return theme * kFxRowFrames + (selected ? 17 : 0) + f; }

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
    bool uiOnly = false;               // a screen control (view, taps): not automatable
};

const MpcParamDef& mpcParamDef(int index);
bool isStepParam(int index);
bool isPictureParam(int index);   // the playhead, waveform columns and tabs: film-strip frames, no text
// The notes pad 1 plays for each MP_PAD_NOTES choice.
int padBaseNote(int choice);

} // namespace bc
