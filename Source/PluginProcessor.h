#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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

    mutable juce::SpinLock transportLock;
    TransportInfo transport;

    mutable juce::CriticalSection stateLock;
    juce::String savedState;
    std::atomic<int> stateVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumAndChordProcessor)
};
