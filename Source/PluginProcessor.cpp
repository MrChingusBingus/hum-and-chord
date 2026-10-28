#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties HumAndChordProcessor::makeBuses()
{
   #if JucePlugin_IsMidiEffect
    return {};
   #else
    return juce::AudioProcessor::BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

HumAndChordProcessor::HumAndChordProcessor()
    : AudioProcessor (makeBuses())
{
    pendingOut.reserve (fifoSize);
}

bool HumAndChordProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

bool HumAndChordProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
   #endif
}

void HumAndChordProcessor::prepareToPlay (double, int)
{
    pendingOut.clear();
}

void HumAndChordProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // this plugin makes no sound of its own

    const int numSamples = juce::jmax (1, buffer.getNumSamples());
    double sampleRate = getSampleRate();
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    // 1. Read the host transport.
    TransportInfo t;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto pos = hostPlayHead->getPosition())
        {
            t.valid = true;
            t.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) t.ppq = *ppq;
            if (auto bpm = pos->getBpm())         t.bpm = *bpm;
            if (auto sig = pos->getTimeSignature())
            {
                t.numerator = sig->numerator;
                t.denominator = sig->denominator;
            }
        }
    }
    {
        const juce::SpinLock::ScopedLockType lock (transportLock);
        transport = t;
    }

    // 2. Copy incoming notes for the editor. The MIDI itself passes through untouched,
    //    so the track's instrument keeps playing your part.
    const double beatsPerSample = t.bpm / 60.0 / sampleRate;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! (msg.isNoteOnOrOff() || msg.isAllNotesOff()))
            continue;

        const auto* raw = msg.getRawData();
        HostEvent e;
        e.status = raw[0];
        e.data1  = msg.getRawDataSize() > 1 ? raw[1] : 0;
        e.data2  = msg.getRawDataSize() > 2 ? raw[2] : 0;
        e.ppq    = t.ppq + metadata.samplePosition * beatsPerSample;

        const auto scope = inFifo.write (1);
        if (scope.blockSize1 > 0)      inBuffer[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) inBuffer[(size_t) scope.startIndex2] = e;
        // If the editor is closed the queue fills up and new notes are dropped.
    }

    // 3. Add chords the editor asked to play.
    {
        const auto scope = outFifo.read (outFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) pendingOut.push_back (outBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) pendingOut.push_back (outBuffer[(size_t) (scope.startIndex2 + i)]);
    }

    if (! pendingOut.empty())
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double blockMs = numSamples * 1000.0 / sampleRate;

        for (auto it = pendingOut.begin(); it != pendingOut.end();)
        {
            if (it->dueMs < nowMs + blockMs)
            {
                const int offset = juce::jlimit (0, numSamples - 1,
                                                 (int) ((it->dueMs - nowMs) * sampleRate / 1000.0));
                midi.addEvent (juce::MidiMessage (it->status, it->data1, it->data2), offset);
                it = pendingOut.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void HumAndChordProcessor::popHostEvents (std::vector<HostEvent>& out)
{
    const auto scope = inFifo.read (inFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) out.push_back (inBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) out.push_back (inBuffer[(size_t) (scope.startIndex2 + i)]);
}

void HumAndChordProcessor::clearHostEvents()
{
    std::vector<HostEvent> discard;
    popHostEvents (discard);
}

TransportInfo HumAndChordProcessor::getTransport() const
{
    const juce::SpinLock::ScopedLockType lock (transportLock);
    return transport;
}

void HumAndChordProcessor::queueOutgoing (juce::uint8 status, juce::uint8 data1, juce::uint8 data2, double delayMs)
{
    const Outgoing o { status, data1, data2, juce::Time::getMillisecondCounterHiRes() + juce::jmax (0.0, delayMs) };
    const auto scope = outFifo.write (1);
    if (scope.blockSize1 > 0)      outBuffer[(size_t) scope.startIndex1] = o;
    else if (scope.blockSize2 > 0) outBuffer[(size_t) scope.startIndex2] = o;
}

void HumAndChordProcessor::sendAllNotesOff()
{
    for (juce::uint8 ch = 0; ch < 2; ++ch)
        queueOutgoing ((juce::uint8) (0xB0 | ch), 123, 0, 0.0);
}

juce::String HumAndChordProcessor::getSavedState() const
{
    const juce::ScopedLock lock (stateLock);
    return savedState;
}

void HumAndChordProcessor::setSavedStateFromEditor (const juce::String& json)
{
    const juce::ScopedLock lock (stateLock);
    savedState = json; // the editor already has this, so the version stays the same
}

void HumAndChordProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (stateLock);
    juce::MemoryOutputStream stream (destData, false);
    stream.writeString (savedState);
}

void HumAndChordProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    {
        const juce::ScopedLock lock (stateLock);
        juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);
        savedState = stream.readString();
    }
    ++stateVersion; // tells an open editor to reload the songs saved in the project
}

juce::AudioProcessorEditor* HumAndChordProcessor::createEditor()
{
    return new HumAndChordEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HumAndChordProcessor();
}
