#include "PluginEditor.h"
#include "BinaryData.h"

HumAndChordEditor::HumAndChordEditor (HumAndChordProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      web (makeOptions())
{
    addAndMakeVisible (web);
    setResizable (true, true);
    setResizeLimits (720, 520, 2400, 1600);
    setSize (1180, 760);

    processor.clearHostEvents();
    scratch.reserve (512);
    web.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (30);
}

HumAndChordEditor::~HumAndChordEditor()
{
    stopTimer();
}

juce::WebBrowserComponent::Options HumAndChordEditor::makeOptions()
{
    auto options = juce::WebBrowserComponent::Options{}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("ready",    [this] (const juce::var&)   { onReady(); })
        .withEventListener ("send",     [this] (const juce::var& v) { onSend (v); })
        .withEventListener ("save",     [this] (const juce::var& v) { onSave (v); })
        .withEventListener ("saveFile", [this] (const juce::var& v) { onSaveFile (v); })
        .withEventListener ("openFile", [this] (const juce::var& v) { onOpenFile (v); })
        .withEventListener ("copy",     [this] (const juce::var& v) { onCopy (v); });

   #if JUCE_WINDOWS
    options = options.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                     .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                         .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                  .getChildFile ("HumAndChordWebView")));
   #endif
    return options;
}

std::optional<juce::WebBrowserComponent::Resource> HumAndChordEditor::getResource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    if (path == "/" || path == "/index.html")
    {
        const auto* bytes = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        return juce::WebBrowserComponent::Resource {
            std::vector<std::byte> (bytes, bytes + BinaryData::index_htmlSize),
            "text/html" };
    }
    return std::nullopt;
}

void HumAndChordEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff10141a));
}

void HumAndChordEditor::resized()
{
    web.setBounds (getLocalBounds());
}

// ---------- page -> plugin ----------

void HumAndChordEditor::onReady()
{
    pageReady = true;
    processor.clearHostEvents();
    sendState();

    auto* info = new juce::DynamicObject();
    info->setProperty ("host", juce::PluginHostType().getHostDescription());
    info->setProperty ("format", juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    info->setProperty ("midiEffect", processor.isMidiEffect());
    web.emitEventIfBrowserIsVisible ("hostInfo", juce::var (info));
}

void HumAndChordEditor::sendState()
{
    sentStateVersion = processor.getStateVersion();
    auto* o = new juce::DynamicObject();
    o->setProperty ("json", processor.getSavedState());
    web.emitEventIfBrowserIsVisible ("state", juce::var (o));
}

void HumAndChordEditor::onSend (const juce::var& v)
{
    if (v.getProperty ("allOff", false))
    {
        processor.sendAllNotesOff();
        return;
    }

    if (const auto* events = v.getProperty ("ev", {}).getArray())
    {
        for (const auto& e : *events)
        {
            const auto* a = e.getArray();
            if (a == nullptr || a->size() < 4)
                continue;

            const int status = (int) (*a)[0];
            if (status < 0x80 || status > 0xEF)
                continue;

            processor.queueOutgoing ((juce::uint8) status,
                                     (juce::uint8) juce::jlimit (0, 127, (int) (*a)[1]),
                                     (juce::uint8) juce::jlimit (0, 127, (int) (*a)[2]),
                                     (double) (*a)[3]);
        }
    }
}

void HumAndChordEditor::onSave (const juce::var& v)
{
    processor.setSavedStateFromEditor (v.getProperty ("json", "").toString());
}

void HumAndChordEditor::onSaveFile (const juce::var& v)
{
    const auto name = v.getProperty ("name", "Hum and Chord").toString();
    auto data = std::make_shared<juce::MemoryOutputStream>();
    juce::Base64::convertFromBase64 (*data, v.getProperty ("b64", "").toString());

    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name);
    chooser = std::make_unique<juce::FileChooser> ("Save " + name, start, "*" + start.getFileExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [data] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  file.replaceWithData (data->getData(), data->getDataSize());
                          });
}

void HumAndChordEditor::onOpenFile (const juce::var& v)
{
    const auto kind = v.getProperty ("kind", "midi").toString();
    const juce::String patterns = kind == "midi" ? "*.mid;*.midi" : "*.json";

    chooser = std::make_unique<juce::FileChooser> (kind == "midi" ? "Open a MIDI file" : "Restore a backup",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   patterns);

    juce::Component::SafePointer<HumAndChordEditor> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, kind] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safeThis == nullptr || ! file.existsAsFile())
                                  return;

                              juce::MemoryBlock block;
                              if (! file.loadFileAsData (block))
                                  return;

                              auto* o = new juce::DynamicObject();
                              o->setProperty ("kind", kind);
                              o->setProperty ("name", file.getFileName());
                              o->setProperty ("b64", juce::Base64::toBase64 (block.getData(), block.getSize()));
                              safeThis->web.emitEventIfBrowserIsVisible ("fileOpened", juce::var (o));
                          });
}

void HumAndChordEditor::onCopy (const juce::var& v)
{
    juce::SystemClipboard::copyTextToClipboard (v.getProperty ("text", "").toString());
}

// ---------- plugin -> page ----------

void HumAndChordEditor::timerCallback()
{
    if (! pageReady)
        return;

    if (processor.getStateVersion() != sentStateVersion)
        sendState(); // the DAW loaded a project while the window was open

    scratch.clear();
    processor.popHostEvents (scratch);
    const auto t = processor.getTransport();

    auto* transport = new juce::DynamicObject();
    transport->setProperty ("valid", t.valid);
    transport->setProperty ("playing", t.playing);
    transport->setProperty ("ppq", t.ppq);
    transport->setProperty ("bpm", t.bpm);
    transport->setProperty ("num", t.numerator);
    transport->setProperty ("den", t.denominator);

    juce::Array<juce::var> events;
    events.ensureStorageAllocated ((int) scratch.size());
    for (const auto& e : scratch)
        events.add (juce::Array<juce::var> { (int) e.status, (int) e.data1, (int) e.data2, e.ppq });

    auto* tick = new juce::DynamicObject();
    tick->setProperty ("t", juce::var (transport));
    tick->setProperty ("ev", events);
    web.emitEventIfBrowserIsVisible ("tick", juce::var (tick));
}
