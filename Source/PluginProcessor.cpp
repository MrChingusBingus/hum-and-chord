#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties HumAndChordProcessor::makeBuses()
{
   #if JucePlugin_IsMidiEffect
    return {};
   #else
    return juce::AudioProcessor::BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
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
    const auto in  = layouts.getMainInputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
   #endif
}

void HumAndChordProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pendingOut.clear();

    // Analysis runs on a 12 kHz copy of the audio: plenty for pitch and chords, and cheap.
    decimation = juce::jmax (1, (int) std::lround (sampleRate / 12000.0));
    decimationPhase = 0;
    analysisRate.store (sampleRate / decimation);
    const auto cutoff = juce::jmin (5000.0, 0.42 * sampleRate / decimation);
    antiAlias1.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, cutoff, 0.54));
    antiAlias2.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, cutoff, 1.31));
    antiAlias1.reset();
    antiAlias2.reset();

    const auto scratchSize = (size_t) juce::jmax (512, samplesPerBlock);
    monoScratch.assign (scratchSize, 0.0f);
    decimatedScratch.assign (scratchSize, 0.0f);
}

void HumAndChordProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Audio passes straight through. Clear any extra outputs that have no matching input.
    const int numInputs = getTotalNumInputChannels();
    for (int ch = numInputs; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

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

    if (numInputs > 0 && buffer.getNumSamples() > 0 && analysisWanted.load())
        feedAnalysis (buffer, numInputs, t);

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

void HumAndChordProcessor::feedAnalysis (const juce::AudioBuffer<float>& buffer, int numInputs, const TransportInfo& t)
{
    if (monoScratch.empty())
        return;

    const Marker marker { samplesWritten, t.ppq, t.bpm, t.playing };
    {
        const auto scope = markerFifo.write (1);
        if (scope.blockSize1 > 0)      markerBuffer[(size_t) scope.startIndex1] = marker;
        else if (scope.blockSize2 > 0) markerBuffer[(size_t) scope.startIndex2] = marker;
    }

    const int total = buffer.getNumSamples();
    const int chunk = (int) monoScratch.size();
    const float gain = 1.0f / (float) numInputs;

    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);

        // Mix to mono and filter out everything above ~5 kHz before downsampling.
        juce::FloatVectorOperations::copyWithMultiply (monoScratch.data(), buffer.getReadPointer (0, start), gain, n);
        for (int ch = 1; ch < numInputs; ++ch)
            juce::FloatVectorOperations::addWithMultiply (monoScratch.data(), buffer.getReadPointer (ch, start), gain, n);
        antiAlias1.processSamples (monoScratch.data(), n);
        antiAlias2.processSamples (monoScratch.data(), n);

        int m = 0;
        for (int i = 0; i < n; ++i)
            if (++decimationPhase >= decimation)
            {
                decimationPhase = 0;
                decimatedScratch[(size_t) m++] = monoScratch[(size_t) i];
            }

        if (m == 0)
            continue;

        if (audioFifo.getFreeSpace() < m)
            return; // the editor fell behind; skip this block rather than block the audio thread

        const auto scope = audioFifo.write (m);
        if (scope.blockSize1 > 0)
            std::copy_n (decimatedScratch.data(), scope.blockSize1, audioBuffer.data() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy_n (decimatedScratch.data() + scope.blockSize1, scope.blockSize2, audioBuffer.data() + scope.startIndex2);
        samplesWritten += m;
    }
}

void HumAndChordProcessor::analyseAudio (std::vector<AudioFrame>& out)
{
    analyser.setSampleRate (analysisRate.load());

    {
        const auto scope = markerFifo.read (markerFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) markers.push_back (markerBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) markers.push_back (markerBuffer[(size_t) (scope.startIndex2 + i)]);
        if (markers.size() > 512)
            markers.erase (markers.begin(), markers.end() - 512);
    }

    const auto firstNew = out.size();
    frameCentres.clear();
    {
        const auto scope = audioFifo.read (audioFifo.getNumReady());
        if (scope.blockSize1 > 0) analyser.push (audioBuffer.data() + scope.startIndex1, scope.blockSize1, out, frameCentres);
        if (scope.blockSize2 > 0) analyser.push (audioBuffer.data() + scope.startIndex2, scope.blockSize2, out, frameCentres);
    }

    // Place each frame in the song using the transport position recorded with its audio.
    const double rate = analyser.getSampleRate();
    for (size_t i = 0; i < frameCentres.size(); ++i)
    {
        auto& frame = out[firstNew + i];
        const auto centre = frameCentres[i];
        const Marker* best = nullptr;
        for (const auto& mk : markers)
            if (mk.index <= centre && (best == nullptr || mk.index >= best->index))
                best = &mk;

        if (best != nullptr)
        {
            frame.playing = best->playing;
            frame.ppq = best->ppq + (best->playing ? (double) (centre - best->index) / rate * best->bpm / 60.0 : 0.0);
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
