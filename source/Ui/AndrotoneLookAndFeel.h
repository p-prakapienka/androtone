#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AndrotoneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AndrotoneLookAndFeel() {
        const juce::Colour background { 0xFF07100F };  // canvas (#07100F)
        const juce::Colour surface    { 0xFF0B1517 };  // app surface (#0B1517)
        const juce::Colour outline    { 0xFF1C3136 };  // hairline border (#1C3136)
        const juce::Colour accent     { 0xFF0097A7 };  // teal 500 (#0097A7)
        const juce::Colour text       { 0xFFE6F0F1 };  // primary text (#E6F0F1)
        const juce::Colour textMuted  { 0xFF6E8A8F };  // secondary text (#6E8A8F)

        setColour(juce::ResizableWindow::backgroundColourId, background);

        setColour(juce::TabbedComponent::backgroundColourId, background);
        setColour(juce::TabbedComponent::outlineColourId, outline);
        setColour(juce::TabbedButtonBar::tabOutlineColourId, outline);
        setColour(juce::TabbedButtonBar::frontOutlineColourId, accent);
        setColour(juce::TabbedButtonBar::tabTextColourId, textMuted);
        setColour(juce::TabbedButtonBar::frontTextColourId, text);

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
};
