#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AndrotoneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // Shared palette (ARGB), so components can reference the same colours the look and feel uses.
    struct Palette {
        static constexpr juce::uint32 BACKGROUND = 0xFF07100F; // canvas (#07100F)
        static constexpr juce::uint32 SURFACE = 0xFF0B1517; // app surface (#0B1517)
        static constexpr juce::uint32 OUTLINE = 0xFF1C3136; // hairline border (#1C3136)
        static constexpr juce::uint32 ACCENT = 0xFF0097A7; // teal 500 (#0097A7)
        static constexpr juce::uint32 TEXT = 0xFFE6F0F1; // primary text (#E6F0F1)
        static constexpr juce::uint32 TEXT_MUTED = 0xFF6E8A8F; // secondary text (#6E8A8F)
    };

    AndrotoneLookAndFeel() {
        const juce::Colour background { Palette::BACKGROUND };
        const juce::Colour surface { Palette::SURFACE };
        const juce::Colour outline { Palette::OUTLINE };
        const juce::Colour accent { Palette::ACCENT };
        const juce::Colour text { Palette::TEXT };
        const juce::Colour textMuted { Palette::TEXT_MUTED };

        setColour(juce::ResizableWindow::backgroundColourId, background);

        setColour(juce::TabbedComponent::backgroundColourId, background);
        setColour(juce::TabbedComponent::outlineColourId, outline);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, outline);
        setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
        setColour(juce::TabbedButtonBar::tabTextColourId, textMuted);
        setColour(juce::TabbedButtonBar::frontTextColourId, accent);  // selected tab text (same as buttonOnColourId)

        setColour(juce::TextButton::buttonColourId, surface);
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, background);

        setColour(juce::Slider::backgroundColourId, surface);
        setColour(juce::Slider::trackColourId, accent);
        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, surface);
        setColour(juce::Slider::textBoxOutlineColourId, outline);
        setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.4f));

        setColour(juce::Label::textColourId, text);
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);

        setColour(juce::PopupMenu::backgroundColourId, surface);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour(juce::PopupMenu::highlightedTextColourId, background);
    }

    int getTabButtonBestWidth(juce::TabBarButton& button, int /*tabDepth*/) override {
        return button.getTabbedButtonBar().getWidth() / 4;  // 4 equal-width tabs
    }

    // Tabs are always on top: fill every tab with the same background and draw the text,
    // using frontTextColourId for the selected tab and tabTextColourId for the rest.
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool, bool) override {
        g.setColour(button.getTabBackgroundColour());
        g.fillRect(button.getActiveArea());

        const auto colourId = button.isFrontTab()
            ? juce::TabbedButtonBar::frontTextColourId
            : juce::TabbedButtonBar::tabTextColourId;

        g.setColour(findColour(colourId));
        g.setFont(getTabButtonFont(button, (float) button.getTextArea().getHeight()));
        g.drawFittedText(
            button.getButtonText().trim(),
            button.getTextArea(),
            juce::Justification::centred,
            1
        );
    }
};
