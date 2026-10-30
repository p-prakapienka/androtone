#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A rotary knob for fingers: drag up/right to turn it; hold still for a moment first (long press)
// to switch to fine adjustment, ten times slower, for the rest of that drag. A ring shows fine mode.
class TouchKnob : public juce::Slider, private juce::Timer {
public:
    static constexpr int longPressMs = 450;
    static constexpr float stillDistance = 6.0f; // pixels a finger may wobble and still count as held
    static constexpr double fineFactor = 10.0;

    TouchKnob() : juce::Slider(RotaryHorizontalVerticalDrag, TextBoxBelow) {}

    bool isFine() const { return fine; }

    void mouseDown(const juce::MouseEvent& event) override {
        fine = false;
        moved = false;
        downPosition = event.position;
        juce::Slider::mouseDown(event);
        if (isEnabled()) {
            startTimer(longPressMs);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!fine) {
            if (event.position.getDistanceFrom(downPosition) > stillDistance) {
                moved = true;
                stopTimer();
            }
            juce::Slider::mouseDrag(event);
            return;
        }
        // Same direction rule as RotaryHorizontalVerticalDrag, scaled down.
        const float pixels = (event.position.x - fineStartPosition.x) + (fineStartPosition.y - event.position.y);
        const double proportion = fineStartProportion + pixels / (getMouseDragSensitivity() * fineFactor);
        setValue(proportionOfLengthToValue(juce::jlimit(0.0, 1.0, proportion)), juce::sendNotificationSync);
    }

    void mouseUp(const juce::MouseEvent& event) override {
        stopTimer();
        juce::Slider::mouseUp(event);
        if (fine) {
            fine = false;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        juce::Slider::paint(g);
        if (fine) {
            const auto knob = getLookAndFeel().getSliderLayout(*this).sliderBounds.toFloat();
            const float size = juce::jmin(knob.getWidth(), knob.getHeight());
            g.setColour(findColour(rotarySliderFillColourId).brighter(0.4f));
            g.drawEllipse(knob.withSizeKeepingCentre(size, size).reduced(1.0f), 2.0f);
        }
    }

private:
    bool fine = false;
    bool moved = false;
    juce::Point<float> downPosition, fineStartPosition;
    double fineStartProportion = 0.0;

    void timerCallback() override {
        stopTimer();
        if (moved || !isMouseButtonDown()) {
            return;
        }
        fine = true;
        fineStartPosition = getMouseXYRelative().toFloat();
        fineStartProportion = valueToProportionOfLength(getValue());
        repaint();
    }
};
