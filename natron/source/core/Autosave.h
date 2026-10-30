#pragma once

#include "Session.h"

// Saves the session's song when the app is backgrounded or closed, and restores it on the next
// launch. Writes go to a temporary file first, so a crash mid-save never loses the previous copy.
namespace Autosave {
    // <app data>/Natron/autosave.natron (the app's private files directory on Android).
    juce::File defaultFile();

    juce::Result save(const Session& session, const juce::File& file);

    // Fails, leaving the session as it was, when there is no autosave or it can't be read.
    juce::Result restore(Session& session, const juce::File& file);
}
