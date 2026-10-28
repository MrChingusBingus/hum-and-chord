#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// The editor is a web view showing the Hum & Chord page. A timer forwards the
// track's notes and transport to the page; the page sends back chords to play,
// songs to save in the project, and files to save or open.
class HumAndChordEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit HumAndChordEditor (HumAndChordProcessor&);
    ~HumAndChordEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::WebBrowserComponent::Options makeOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void onReady();
    void onSend (const juce::var&);
    void onSave (const juce::var&);
    void onSaveFile (const juce::var&);
    void onOpenFile (const juce::var&);
    void onCopy (const juce::var&);
    void sendState();

    HumAndChordProcessor& processor;
    juce::WebBrowserComponent web;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<HostEvent> scratch;
    std::vector<AudioFrame> frames;
    bool pageReady = false;
    int sentStateVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumAndChordEditor)
};
