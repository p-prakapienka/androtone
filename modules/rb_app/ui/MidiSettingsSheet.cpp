namespace rb
{

namespace
{
    struct SectionLabel : public juce::Component
    {
        explicit SectionLabel (juce::String t) : text (std::move (t)) {}
        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::get().textDim);
            g.setFont (Theme::font (13.0f, true));
            g.drawText (text, getLocalBounds(), juce::Justification::bottomLeft);
        }
        juce::String text;
    };

    struct DeviceRow : public juce::Component
    {
        DeviceRow (Studio& s, MidiInputs::Device d) : studio (s), device (std::move (d))
        {
            toggle.setButtonText (device.info.name);
            toggle.setToggleState (device.enabled, juce::dontSendNotification);
            toggle.onClick = [this] { studio.getMidiInputs().setEnabled (device.info.identifier, toggle.getToggleState()); };
            addAndMakeVisible (toggle);
        }
        void resized() override { toggle.setBounds (getLocalBounds()); }

        Studio& studio;
        MidiInputs::Device device;
        juce::ToggleButton toggle;
    };

    struct MappingRow : public juce::Component
    {
        MappingRow (Studio& s, juce::String t) : studio (s), target (std::move (t))
        {
            addAndMakeVisible (learnButton);
            addAndMakeVisible (clearButton);
            learnButton.onClick = [this]
            {
                auto& midi = studio.getMidiInputs();
                if (midi.getLearningTarget() == target) midi.cancelLearning();
                else midi.startLearning (target);
            };
            clearButton.onClick = [this] { studio.getMidiInputs().clearMapping (target); };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::get();
            const auto mapping = studio.getSettings().getMidiMapping (target);
            const bool learning = studio.getMidiInputs().getLearningTarget() == target;
            learnButton.setStyle (learning ? PillButton::Style::accent : PillButton::Style::normal);
            learnButton.setButtonText (learning ? "Move a control" : "Map");
            clearButton.setVisible (mapping.isNotEmpty());

            auto area = getLocalBounds().withTrimmedRight (210);
            g.setColour (t.text);
            g.setFont (Theme::font (16.0f));
            g.drawText (target, area.removeFromLeft (area.getWidth() / 2), juce::Justification::centredLeft);

            juce::String text = "-";
            auto parts = juce::StringArray::fromTokens (mapping, ":", {});
            if (parts.size() == 3)
                text = (parts[0] == "cc" ? "CC " : "Note ") + parts[2] + " (ch " + parts[1] + ")";
            g.setColour (t.textDim);
            g.setFont (Theme::font (13.0f));
            g.drawText (text, area, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (0, 4);
            clearButton.setBounds (b.removeFromRight (Theme::touch));
            learnButton.setBounds (b.removeFromRight (160));
        }

        Studio& studio;
        juce::String target;
        PillButton learnButton { "Map" };
        IconButton clearButton { Icon::close };
    };
}

MidiSettingsSheet::MidiSettingsSheet (Studio& s) : Sheet ("MIDI Input"), studio (s)
{
    heightFraction = 0.9f;
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    content.addAndMakeVisible (bluetoothButton);
    bluetoothButton.onClick = []
    {
        if (juce::BluetoothMidiDevicePairingDialogue::isAvailable())
            juce::BluetoothMidiDevicePairingDialogue::open();
    };
    bluetoothButton.setEnabled (juce::BluetoothMidiDevicePairingDialogue::isAvailable());

    studio.getMidiInputs().onChange = [safe = juce::Component::SafePointer<MidiSettingsSheet> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->rebuild(); });
    };

    rebuild();
    startTimerHz (4);
}

MidiSettingsSheet::~MidiSettingsSheet()
{
    studio.getMidiInputs().cancelLearning();
    studio.getMidiInputs().onChange = nullptr;
}

void MidiSettingsSheet::rebuild()
{
    rows.clear();

    rows.push_back (std::make_unique<SectionLabel> ("DEVICES"));
    const auto devices = studio.getMidiInputs().getDevices();
    for (auto& d : devices)
        rows.push_back (std::make_unique<DeviceRow> (studio, d));
    if (devices.empty())
        rows.push_back (std::make_unique<SectionLabel> ("No MIDI devices connected. Plug in a USB controller or pair a Bluetooth one."));

    rows.push_back (std::make_unique<SectionLabel> ("MAPPINGS - tap Map, then move a control"));
    for (auto& target : MidiInputs::getTargets())
        rows.push_back (std::make_unique<MappingRow> (studio, target));

    for (auto& r : rows)
        content.addAndMakeVisible (*r);
    resized();
}

void MidiSettingsSheet::timerCallback()
{
    const auto learning = studio.getMidiInputs().getLearningTarget();
    if (learning != lastLearning)
    {
        lastLearning = learning;
        content.repaint();
        for (auto& r : rows)
            r->repaint();
    }
}

void MidiSettingsSheet::resized()
{
    Sheet::resized();
    auto area = getContentArea();
    viewport.setBounds (area);

    const int w = area.getWidth() - 8;
    int y = 0;
    bluetoothButton.setBounds (0, y, w, Theme::touch);
    y += Theme::touch + 8;

    for (auto& r : rows)
    {
        const bool label = dynamic_cast<SectionLabel*> (r.get()) != nullptr;
        const int h = label ? 34 : Theme::touch + 4;
        r->setBounds (0, y, w, h);
        y += h + 2;
    }
    content.setSize (w, y + 8);
}

} // namespace rb
