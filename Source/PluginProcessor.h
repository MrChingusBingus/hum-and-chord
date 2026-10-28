#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioAnalyser.h"
#include <array>
#include <atomic>
#include <vector>

// One MIDI message from the host, stamped with its position in beats (quarter notes).
struct HostEvent
{
    juce::uint8 status = 0, data1 = 0, data2 = 0;
    double ppq = 0.0;
};

// What the host's transport was doing at the last audio block.
struct TransportInfo
{
    bool valid = false;
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
};

class HumAndChordProcessor final : public juce::AudioProcessor
{
public:
    HumAndChordProcessor();
    ~HumAndChordProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread) ----
    void popHostEvents (std::vector<HostEvent>& out);
    void clearHostEvents();
    TransportInfo getTransport() const;
    // Queue a MIDI message to send out of the plugin after delayMs milliseconds.
    void queueOutgoing (juce::uint8 status, juce::uint8 data1, juce::uint8 data2, double delayMs);
    void sendAllNotesOff();

    // Audio analysis (for plugins on audio tracks). The editor turns it on while open.
    void setAnalysisWanted (bool shouldAnalyse) noexcept { analysisWanted.store (shouldAnalyse); }
    bool hasAudioInput() const { return getTotalNumInputChannels() > 0; }
    void analyseAudio (std::vector<AudioFrame>& out); // message thread only

    juce::String getSavedState() const;
    void setSavedStateFromEditor (const juce::String& json);
    int getStateVersion() const noexcept { return stateVersion.load(); }

private:
    static BusesProperties makeBuses();
    static constexpr int fifoSize = 4096;

    juce::AbstractFifo inFifo { fifoSize };
    std::array<HostEvent, fifoSize> inBuffer;

    struct Outgoing { juce::uint8 status, data1, data2; double dueMs; };
    juce::AbstractFifo outFifo { fifoSize };
    std::array<Outgoing, fifoSize> outBuffer;
    std::vector<Outgoing> pendingOut; // audio thread only

    // ---- audio analysis: audio thread side ----
    void feedAnalysis (const juce::AudioBuffer<float>&, int numInputs, const TransportInfo&);
    static constexpr int audioFifoSize = 1 << 17;
    std::atomic<bool> analysisWanted { false };
    std::atomic<double> analysisRate { 12000.0 };
    int decimation = 4, decimationPhase = 0;
    juce::IIRFilter antiAlias1, antiAlias2;
    std::vector<float> monoScratch, decimatedScratch;
    juce::AbstractFifo audioFifo { audioFifoSize };
    std::vector<float> audioBuffer = std::vector<float> ((size_t) audioFifoSize, 0.0f);
    struct Marker { juce::int64 index; double ppq, bpm; bool playing; };
    juce::AbstractFifo markerFifo { 1024 };
    std::array<Marker, 1024> markerBuffer;
    juce::int64 samplesWritten = 0;

    // ---- audio analysis: message thread side ----
    AudioAnalyser analyser;
    std::vector<Marker> markers;
    std::vector<juce::int64> frameCentres;

    mutable juce::SpinLock transportLock;
    TransportInfo transport;

    mutable juce::CriticalSection stateLock;
    juce::String savedState;
    std::atomic<int> stateVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumAndChordProcessor)
};
