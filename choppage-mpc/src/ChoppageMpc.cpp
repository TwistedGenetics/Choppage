// CHOPPAGE for Akai standalone MPCs: the MPC Plugin SDK adapter (MPC OS 3.10.1+, Gen1 32-bit and Gen2 64-bit).
//
// This file is the whole MPC-specific layer: it implements bc::IPlatform on top of mpc::Plugin and forwards audio,
// MIDI, parameters and state to ChoppageMpcApp (platform-independent, tested on the desktop).
//
// Pads: pads 1-8 (bottom two rows) pick and play the 8 breaks, pads 9-16 (top two rows) hold the 8 FX.
// The skin (Plugin Skins/, made by tools/make_skin.py) draws the S950 front panel and its LCD.
#include "mpc_plugin.h"
#include "mpc_resources.h"
#include "ChoppageMpcApp.h"
#include <memory>

using namespace bc;

static mpc::Param toSdkParam(int index)
{
    const MpcParamDef& d = mpcParamDef(index);
    mpc::Param p;
    switch (d.kind)
    {
        case MpcKind::Bool: p = mpc::boolParam(d.id.c_str(), d.name.c_str(), d.def >= 0.5f); break;
        case MpcKind::Choice: p = mpc::choiceParam(d.id.c_str(), d.name.c_str(), d.choices, (int)d.def); break;
        case MpcKind::Int: p = mpc::intParam(d.id.c_str(), d.name.c_str(), (int)d.min, (int)d.max, (int)d.def); break;
        default: p = mpc::floatParam(d.id.c_str(), d.name.c_str(), d.min, d.max, d.def, "", 2, d.skew); break;
    }
    if (d.hidden || d.readout || d.trigger || d.uiOnly) p.automatable = false;
    return p;
}

class ChoppageMpc : public mpc::Plugin, public IPlatform
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "CHOPPAGE";
        i.vendor = "Twisted Genetics";
        i.uniqueId = MPC_FOURCC('T', 'g', 'C', 'h');   // == "uid" in plugin.json
        i.version = 7000;                               // 0.7.0
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        i.tailSamples = 0;
        return i;
    }

    ChoppageMpc() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < kMpcNumParams; ++i) addParam(toSdkParam(i));
        app_ = std::make_unique<ChoppageMpcApp>(*this);
        for (int i = 0; i < kMpcNumParams; ++i) app_->setParam(i, get(i));
    }

    ~ChoppageMpc() override { app_.reset(); }

    // ---- mpc::Plugin
    void prepare(double sampleRate, int maxBlockSize) override { app_->prepare(sampleRate, maxBlockSize); }
    void parameterChanged(int index, float plain) override { if (app_) app_->setParam(index, plain); }
    bool formatValue(int index, float plain, std::string& out) override { return app_ && app_->formatValue(index, plain, out); }

    void process(mpc::ProcessContext& ctx) override
    {
        MidiMsg buf[kMaxMidi];
        int n = 0;
        for (int i = 0; i < ctx.numMidiEvents && n < kMaxMidi; ++i)
        {
            const mpc::MidiEvent& e = ctx.midi[i];
            MidiMsg m;
            m.offset = e.sampleOffset;
            m.data[0] = e.data[0]; m.data[1] = e.data[1]; m.data[2] = e.data[2];
            buf[n++] = m;
        }
        ChoppageMpcApp::Transport tr;
        tr.valid = ctx.transport.valid && ctx.transport.tempoValid;
        tr.playing = ctx.transport.playing;
        tr.bpm = ctx.transport.bpm;
        tr.ppq = ctx.transport.ppqValid ? ctx.transport.ppqPos : 0.0;
        float* l = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* r = ctx.numOutputs > 1 ? ctx.outputs[1] : l;
        if (!l) return;
        app_->process(l, r, ctx.numFrames, buf, n, tr);
    }

    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string s = app_->saveState();
        out.assign(s.begin(), s.end());
    }
    void loadExtraState(const uint8_t* data, size_t size) override { app_->loadState(std::string((const char*)data, size)); }

    // ---- IPlatform
    const char* platformName() const override { return "MPC (Plugin SDK)"; }
    std::string contentDir() const override { return mpc::contentFolder(); }
    void notifyParameter(int index, float plainValue) override
    {
        set(index, plainValue);            // framework value (the host reads this) -> parameterChanged -> app
        notifyHostParameterChanged(index); // the screen follows
    }

private:
    static constexpr int kMaxMidi = 1024;
    std::unique_ptr<ChoppageMpcApp> app_;
};

MPC_PLUGIN_ENTRY(ChoppageMpc)
