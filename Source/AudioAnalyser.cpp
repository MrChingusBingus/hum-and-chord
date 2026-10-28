#include "AudioAnalyser.h"
#include <cmath>

AudioAnalyser::AudioAnalyser()
    : ring ((size_t) windowSize, 0.0f),
      linear ((size_t) windowSize, 0.0f),
      yin ((size_t) windowSize / 2, 0.0f),
      fftData ((size_t) windowSize * 2, 0.0f),
      window ((size_t) windowSize, 0.0f)
{
    for (int i = 0; i < windowSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (windowSize - 1));
}

void AudioAnalyser::setSampleRate (double rate)
{
    sampleRate = rate > 1000.0 ? rate : 12000.0;
}

void AudioAnalyser::push (const float* samples, int numSamples, std::vector<AudioFrame>& out,
                          std::vector<juce::int64>& frameCentres)
{
    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) ringPos] = samples[i];
        ringPos = (ringPos + 1) % windowSize;
        ++samplesRead;

        if (++sinceHop >= hopSize)
        {
            sinceHop = 0;
            if (samplesRead >= windowSize)
            {
                AudioFrame frame;
                analyse (frame);
                out.push_back (frame);
                frameCentres.push_back (samplesRead - windowSize / 4); // weighted toward the newest audio
            }
        }
    }
}

void AudioAnalyser::analyse (AudioFrame& frame)
{
    // Oldest to newest.
    for (int i = 0; i < windowSize; ++i)
        linear[(size_t) i] = ring[(size_t) ((ringPos + i) % windowSize)];

    // Loudness and pitch from the newest half (~170 ms) so notes respond quickly.
    const int half = windowSize / 2;
    const float* recent = linear.data() + half;
    double sum = 0.0;
    for (int i = 0; i < half; ++i)
        sum += (double) recent[i] * recent[i];
    frame.rms = (float) std::sqrt (sum / half);

    if (frame.rms > 0.003f)
    {
        float aperiodicity = 1.0f;
        const float hz = findPitch (recent, half, aperiodicity);
        frame.aperiodicity = aperiodicity;
        if (hz > 0.0f)
            frame.pitch = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    }

    // Chroma from the whole window.
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < windowSize; ++i)
        fftData[(size_t) i] = linear[(size_t) i] * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double binHz = sampleRate / windowSize;
    const int lowBin  = juce::jmax (1, (int) std::ceil (55.0 / binHz));
    const int highBin = juce::jmin (windowSize / 2 - 1, (int) std::floor (2000.0 / binHz));
    float total = 0.0f, bassTotal = 0.0f;

    for (int k = lowBin; k <= highBin; ++k)
    {
        const float mag = fftData[(size_t) k];
        // Only count local peaks, so the spread around each note doesn't leak into its neighbours.
        if (mag <= fftData[(size_t) (k - 1)] || mag < fftData[(size_t) (k + 1)])
            continue;

        const double hz = k * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
        const float energy = mag * mag;
        frame.chroma[(size_t) pc] += energy;
        total += energy;

        if (hz < 250.0)
        {
            frame.bass[(size_t) pc] += energy;
            bassTotal += energy;
        }
    }

    if (total > 0.0f)     for (auto& c : frame.chroma) c /= total;
    if (bassTotal > 0.0f) for (auto& c : frame.bass)   c /= bassTotal;
}

// YIN pitch detection. Returns Hz, or 0 if there's no clear single pitch.
float AudioAnalyser::findPitch (const float* x, int length, float& aperiodicityOut)
{
    const int w = length / 2;
    const int tauMin = juce::jmax (2, (int) (sampleRate / 1100.0));
    const int tauMax = juce::jmin (w - 1, (int) (sampleRate / 55.0));

    yin[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float d = 0.0f;
        for (int i = 0; i < w; ++i)
        {
            const float diff = x[i] - x[i + tau];
            d += diff * diff;
        }
        running += d;
        yin[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
    }

    int best = -1;
    float lowest = 1.0f;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        lowest = juce::jmin (lowest, yin[(size_t) tau]);
        if (yin[(size_t) tau] < 0.15f)
        {
            while (tau + 1 <= tauMax && yin[(size_t) (tau + 1)] < yin[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        aperiodicityOut = lowest;
        return 0.0f;
    }

    aperiodicityOut = yin[(size_t) best];
    float refined = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float s0 = yin[(size_t) (best - 1)], s1 = yin[(size_t) best], s2 = yin[(size_t) (best + 1)];
        const float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (std::abs (denom) > 1e-9f)
            refined += (s2 - s0) / denom;
    }
    return (float) (sampleRate / refined);
}
