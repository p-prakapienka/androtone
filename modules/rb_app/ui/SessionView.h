#pragma once

namespace rb
{

// The session grid (SES, TRK, CLIP, SCN, MIX): tracks as columns, scenes as rows.
class SessionView : public juce::Component,
                    private Studio::Listener,
                    private juce::Timer
{
public:
    SessionView (Studio&, Navigator&);
    ~SessionView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drag and drop between cells and headers (long-press then drag).
    struct DragState
    {
        enum class Kind { none, clip, track, scene } kind = Kind::none;
        int trackId = 0;
        int slot = 0;
        juce::Point<int> position; // in SessionView coordinates
    };

    void beginDrag (DragState::Kind, int trackId, int slot, juce::Point<int> screenPos);
    void updateDrag (juce::Point<int> screenPos);
    void endDrag (juce::Point<int> screenPos);

    Studio& getStudio() { return studio; }
    Navigator& getNavigator() { return nav; }
    bool areSceneNumbersShown() const { return showScenes; }

private:
    class TrackColumn;
    class SceneColumn;
    class AddTrackColumn;

    void studioChanged() override;
    void timerCallback() override;
    void rebuild();
    void layoutGrid();
    void updateTransportButton();

    Studio& studio;
    Navigator& nav;

    HeaderBar header;
    IconButton settingsButton { Icon::gear };

    juce::Viewport viewport;
    juce::Component grid;
    std::vector<std::unique_ptr<TrackColumn>> columns;
    std::unique_ptr<SceneColumn> sceneColumn;
    std::unique_ptr<AddTrackColumn> addColumn;

    IconButton undoButton { Icon::undo }, redoButton { Icon::redo }, mixerButton { Icon::mixer },
               scenesButton { Icon::scenes }, playButton { Icon::play };

    bool showMixer = false, showScenes = true;
    DragState drag;
    std::vector<int> builtTrackIds;
};

} // namespace rb
