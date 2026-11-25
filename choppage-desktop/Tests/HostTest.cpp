/*
    ChoppageHostTest — loads the BUILT plugin (VST3, or the installed AU
    on macOS) through JUCE's plugin hosting, the same boundary Logic / Ableton use:
      * host save -> new instance -> host restore (project save / duplicate track)
      * the sample travels inside the saved state
      * MIDI in -> audio out across the plugin boundary
    Usage: ChoppageHostTest /path/to/Choppage.vst3
           ChoppageHostTest CHOPPAGE.component
*/
#include <JuceHeader.h>
#include <iostream>

static int failures = 0, checks = 0;
static void check (bool ok, const juce::String& what)
{
    ++checks; if (! ok) ++failures;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << std::endl;
}

/* Hands raw plugin state to a hosted instance, wrapped the way that host format
   stores it: VST3 = JUCE's <VST3PluginState> XML with an IComponent chunk;
   AU = the ClassInfo property list with a jucePluginState <data> entry. */
static bool injectRawState (juce::AudioPluginInstance& inst, const juce::MemoryBlock& raw)
{
    juce::MemoryBlock current;
    inst.getStateInformation (current);

    if (auto xml = juce::AudioProcessor::getXmlFromBinary (current.getData(), (int) current.getSize()))
    {
        if (xml->hasTagName ("VST3PluginState"))
        {
            juce::XmlElement state ("VST3PluginState");
            state.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
            juce::MemoryBlock wrapped;
            juce::AudioProcessor::copyXmlToBinary (state, wrapped);
            inst.setStateInformation (wrapped.getData(), (int) wrapped.getSize());
            return true;
        }
    }

   #if JUCE_MAC
    // AU: the ClassInfo dictionary is usually a binary plist. Let macOS's plutil
    // turn it into XML (the host accepts either), then patch it below.
    if (current.getSize() > 8 && std::memcmp (current.getData(), "bplist", 6) == 0)
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("choppage-au-state.plist");
        tmp.replaceWithData (current.getData(), current.getSize());
        juce::ChildProcess p;
        if (p.start (juce::StringArray { "/usr/bin/plutil", "-convert", "xml1", tmp.getFullPathName() }))
        {
            p.waitForProcessToFinish (10000);
            current.reset();
            tmp.loadFileAsData (current);
        }
    }
   #endif

    const auto text = current.toString();
    if (text.contains ("<plist"))
    {
        auto plist = juce::XmlDocument::parse (text);
        if (plist == nullptr) return false;
        auto* dict = plist->getChildByName ("dict");
        if (dict == nullptr) return false;
        for (int i = 0; i < dict->getNumChildElements() - 1; ++i)
        {
            auto* key = dict->getChildElement (i);
            if (key->hasTagName ("key") && key->getAllSubText() == "jucePluginState")
            {
                auto* data = dict->getChildElement (i + 1);
                data->deleteAllChildElements();
                data->addTextElement (juce::Base64::toBase64 (raw.getData(), raw.getSize()));
                const auto out = plist->toString();
                inst.setStateInformation (out.toRawUTF8(), (int) out.getNumBytesAsUTF8());
                return true;
            }
        }
    }
    return false;
}

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name) return prm;
    return nullptr;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::cout << "usage: ChoppageHostTest plugin.vst3|plugin.component\n"; return 2; }

    juce::AudioPluginFormatManager fm;
    const juce::String path (argv[1]);
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    if (path.endsWith (".component")) fm.addFormat (new juce::AudioUnitPluginFormat());
    else
   #endif
    fm.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> types;
    auto* format = fm.getFormat (0);
    if (path.endsWith (".component"))
    {
        for (auto& id : format->searchPathsForPlugins ({}, true, false))
            if (id.containsIgnoreCase ("CHOPPAGE") || id.containsIgnoreCase ("Chpg"))
                format->findAllTypesForFile (types, id);
    }
    else
    {
        format->findAllTypesForFile (types, path);
    }
    if (types.isEmpty()) { std::cout << "could not load " << path << "\n"; return 2; }

    auto make = [&]
    {
        juce::String err;
        auto inst = fm.createPluginInstance (*types[0], 48000.0, 512, err);
        if (inst == nullptr) std::cout << "instance error: " << err << "\n";
        else inst->prepareToPlay (48000.0, 512);
        return inst;
    };
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    auto block = [] (juce::AudioPluginInstance& p, juce::MidiBuffer m = {})
    {
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        p.processBlock (b, m);
        return b.getRMSLevel (0, 0, 512);
    };

    std::cout << "== Host round trip (" << types[0]->name << ", " << types[0]->pluginFormatName << ") ==\n";
    check (types[0]->isInstrument, "host sees an instrument");

    auto a = make();
    if (a == nullptr) return 2;
    auto* amount = findParam (*a, "Pattern Amount");
    auto* rate   = findParam (*a, "Sample Rate");
    auto* filter = findParam (*a, "Filter");
    check (amount && rate && filter, "host sees Pattern Amount / Sample Rate / Filter");
    if (! (amount && rate && filter)) return 1;
    std::cout << "  params visible to host: " << a->getParameters().size() << "\n";

    // No transport here: a tap on C1 latches the demo break on the internal clock.
    auto run = [&] (juce::AudioPluginInstance& p, int blocks, juce::MidiBuffer first = {})
    {
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            juce::AudioBuffer<float> b (2, 512);
            b.clear();
            juce::MidiBuffer m = i == 0 ? first : juce::MidiBuffer();
            p.processBlock (b, m);
            peak = juce::jmax (peak, b.getMagnitude (0, 512));
        }
        return peak;
    };
    juce::MidiBuffer c1; c1.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
    check (run (*a, 4) < 1e-4f, "silent until a break is played (no host transport)");
    check (run (*a, 40, c1) > 0.01f, "C1 in -> the demo break plays");

    amount->setValueNotifyingHost (0.8f);
    rate->setValueNotifyingHost (rate->getValueForText ("12000"));
    filter->setValueNotifyingHost (0.6f);
    run (*a, 2); pump (100);
    std::cout << "  A: amount=" << amount->getCurrentValueAsText() << " rate=" << rate->getCurrentValueAsText() << "\n";

    juce::MemoryBlock blob;
    a->getStateInformation (blob);
    std::cout << "  state blob: " << (int) blob.getSize() << " bytes\n";
    check (blob.getSize() > 200, "state saves");

    auto b = make();
    b->setStateInformation (blob.getData(), (int) blob.getSize());
    pump (200);
    auto* amountB = findParam (*b, "Pattern Amount");
    auto* rateB = findParam (*b, "Sample Rate");
    check (std::abs (amountB->getValue() - amount->getValue()) < 1e-4f, "new instance restores Pattern Amount");
    check (std::abs (rateB->getValue() - rate->getValue()) < 1e-4f, "new instance restores Sample Rate");
    check (run (*b, 40, c1) > 0.01f, "restored instance plays");

    std::unique_ptr<juce::AudioProcessorEditor> ed (b->createEditorIfNeeded());
    pump (150);
    check (ed != nullptr, "editor opens");
    b->setStateInformation (blob.getData(), (int) blob.getSize());
    pump (150);
    check (run (*b, 40, c1) > 0.01f, "restore with the editor open");
    ed.reset();

    a.reset(); b.reset();
    std::cout << "\n" << (checks - failures) << " / " << checks << " host checks passed\n";
    return failures == 0 ? 0 : 1;
}
