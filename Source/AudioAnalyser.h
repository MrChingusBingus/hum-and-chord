#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// One analysis frame (about 43 ms apart).
struct AudioFrame
{
    double ppq = 0.0;          // where in the song this frame sits, in beats
    bool playing = false;      // host transport was running
    float rms = 0.0f;          // loudness
    float pitch = 0.0f;        // MIDI note number (fractional), 0 if none found
    float aperiodicity = 1.0f; // 0 = perfectly clear single pitch, 1 = noise or chords
    std::array<float, 12> chroma {}; // how much of each note (C..B) is sounding, sums to 1
    std::array<float, 12> bass {};   // same, for 40-250 Hz only
};

// Runs on the message thread. Takes the downsampled mono signal the audio thread
// collected and turns it into frames: a pitch for single-note parts (voice, bass, lead)
// and a 12-note "chroma" for chords and full mixes.
class AudioAnalyser
{
public:
    static constexpr int windowSize = 4096; // ~340 ms at 12 kHz
    static constexpr int hopSize = 512;     // ~43 ms at 12 kHz

    AudioAnalyser();

    void setSampleRate (double rate);
    double getSampleRate() const noexcept { return sampleRate; }

    // Feed new samples. Each finished frame is appended to `out` (without ppq/playing),
    // with the sample index it represents appended to `frameCentres`.
    void push (const float* samples, int numSamples, std::vector<AudioFrame>& out,
               std::vector<juce::int64>& frameCentres);

    juce::int64 getSamplesRead() const noexcept { return samplesRead; }

private:
    void analyse (AudioFrame& frame);
    float findPitch (const float* x, int length, float& aperiodicityOut);

    double sampleRate = 12000.0;
    std::vector<float> ring, linear, yin, fftData, window;
    int ringPos = 0, sinceHop = 0;
    juce::int64 samplesRead = 0;
    juce::dsp::FFT fft { 12 }; // 4096 points
};
