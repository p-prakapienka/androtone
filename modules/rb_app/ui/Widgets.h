#pragma once

namespace rb
{

class Studio;

//==============================================================================
// A round-cornered button showing an icon (and optionally a short label under it).
class IconButton : public juce::Button
{
public:
    explicit IconButton (Icon icon, const juce::String& label = {});

    void setIcon (Icon i) { icon = i; repaint(); }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setIconColour (std::optional<juce::Colour> c) { iconColour = c; repaint(); }
    void setFilled (bool f) { filled = f; repaint(); }      // raised background
    void setHighlight (bool h) { highlight = h; repaint(); } // accent background

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    juce::String label;
    std::optional<juce::Colour> iconColour;
    bool filled = false, highlight = false;
};

//==============================================================================
// Text button with a pill shape and a style.
class PillButton : public juce::Button
{
public:
    enum class Style { normal, accent, record, subtle, outline };

    explicit PillButton (const juce::String& text, Style style = Style::normal);
    void setStyle (Style s) { style = s; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
};

//==============================================================================
// Detects a press held without moving (used for long-press menus and drag starts).
class LongPress : private juce::Timer
{
public:
    std::function<void (juce::Point<int> position)> onLongPress;
    int delayMs = 450;
    int moveTolerance = 10;

    void mouseDown (const juce::MouseEvent& e);
    void mouseDrag (const juce::MouseEvent& e);
    // Returns true if the long press fired during this gesture.
    bool mouseUp();
    bool hasFired() const { return fired; }
    void cancel() { stopTimer(); }

private:
    void timerCallback() override;
    juce::Point<int> downPosition;
    bool fired = false;
};

//==============================================================================
// Knob for one device parameter (DEV-10): drag to change, double-tap to reset, single tap
// for options (reset, insert/delete automation). Edits go to the document as one undo step
// per drag, mark the preset as modified, and record automation while a clip is playing.
class ParamKnob : public juce::Component,
                  private juce::Timer
{
public:
    ParamKnob (Studio&, int trackId, ParamTarget target);

    void setTarget (int trackId, ParamTarget target);
    ParamTarget getTarget() const { return target; }
    void setShowMacroLock (bool b) { macroLock = b; repaint(); }
    void setCompact (bool b) { compact = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Shared helpers to read/write parameters of a track.
    static const ParamSpec* specFor (const Set&, int trackId, const ParamTarget&);
    static float valueFor (const Set&, int trackId, const ParamTarget&);
    static void setValue (Set&, int trackId, const ParamTarget&, float value);

private:
    void timerCallback() override;
    void showOptions();
    float displayValue() const;
    bool isAutomated() const;

    Studio& studio;
    int trackId;
    ParamTarget target;

    bool dragging = false, macroLock = false, compact = false;
    juce::Point<int> downPosition;
    float dragStartNormalised = 0.0f;
    int pendingTaps = 0;
};

//==============================================================================
// Base for modal bottom sheets shown over the main view (settings, browser, export...).
class Sheet : public juce::Component
{
public:
    explicit Sheet (const juce::String& title);

    std::function<void()> onClose;
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;

    // The area below the title bar for subclasses to lay out.
    juce::Rectangle<int> getContentArea() const;
    juce::String title;

    // Preferred height as a fraction of the parent (1 = full screen).
    float heightFraction = 0.8f;

protected:
    IconButton closeButton { Icon::close };
};

//==============================================================================
// Header with a back arrow, a title and space for buttons on the right.
class HeaderBar : public juce::Component
{
public:
    HeaderBar();

    std::function<void()> onBack;
    std::function<void()> onTitleClicked;

    void setTitle (const juce::String& t, bool modified = false);
    void setShowBack (bool b);
    void addRightButton (juce::Component& c) { rightComponents.push_back (&c); addAndMakeVisible (c); resized(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    IconButton backButton { Icon::back };
    juce::String title;
    bool modified = false;
    juce::Rectangle<int> titleArea;
    std::vector<juce::Component*> rightComponents;
};

// Shows a menu suited to touch next to a component.
void showMenu (juce::PopupMenu&, juce::Component& target, std::function<void (int)> onResult);

// Asks for a line of text (rename, preset name).
void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onDone);

} // namespace rb
