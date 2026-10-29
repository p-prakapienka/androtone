#pragma once

namespace rb
{

// Owns the open set on the message thread: edits with undo/redo (GLB-1), autosave (LIB-5),
// and publishing engine snapshots to the SessionProcessor after every change.
class Document : private juce::Timer
{
public:
    Document (SessionProcessor&, SampleLibrary&);
    ~Document() override;

    //==============================================================================
    // Opens a set stored in folder (or creates it there). Clears undo history.
    void open (const juce::File& setFolder, const Set& initialIfMissing);
    // No autosave (tests, export). sampleFolder resolves the set's own samples.
    void openInMemory (const Set&, const juce::File& sampleFolder = {});
    void close();

    const Set& get() const { return set; }
    juce::File getFolder() const { return folder; }
    bool isOpen() const { return isOpenFlag; }

    //==============================================================================
    // Applies an edit as one undo step. Inside a gesture, consecutive edits share one step.
    void edit (const std::function<void (Set&)>& change);

    // Edits that should not be undoable (e.g. selected pad, editor state).
    void editWithoutUndo (const std::function<void (Set&)>& change);

    void beginGesture();
    void endGesture();

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

    //==============================================================================
    void saveNow();

    // Resolves a sample reference against this set's folder.
    SamplePtr getSample (const juce::String& ref);

    //==============================================================================
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void documentChanged() = 0;
    };

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Rebuilds and publishes the engine snapshot (also called after a device restart).
    void publish();

    SessionProcessor& getSession() { return session; }
    SampleLibrary& getSamples() { return samples; }

private:
    void changed (bool markDirty = true);
    void timerCallback() override;
    SnapshotPtr buildSnapshot();

    struct Runtime
    {
        InstrumentType instrumentType = InstrumentType::analog;
        std::shared_ptr<InstrumentProcessor> instrument;
        std::array<EffectType, 2> effectTypes { EffectType::none, EffectType::none };
        std::array<std::shared_ptr<EffectProcessor>, 2> effects;
    };

    SessionProcessor& session;
    SampleLibrary& samples;

    Set set;
    juce::File folder;
    bool isOpenFlag = false;
    bool autosave = false;
    bool dirty = false;

    std::vector<Set> undoStack, redoStack;
    int gestureDepth = 0;
    bool gesturePushed = false;
    static constexpr size_t maxUndo = 200;

    std::map<int, Runtime> runtimes;
    juce::uint64 nextSequence = 1;

    juce::ListenerList<Listener> listeners;
};

} // namespace rb
