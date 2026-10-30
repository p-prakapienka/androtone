#include "Autosave.h"

juce::File Autosave::defaultFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Natron")
        .getChildFile("autosave.natron");
}

juce::Result Autosave::save(const Session& session, const juce::File& file) {
    if (const auto created = file.getParentDirectory().createDirectory(); created.failed()) {
        return created;
    }
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream stream(temporary.getFile());
        if (!stream.openedOk()) {
            return juce::Result::fail("Could not write " + temporary.getFile().getFullPathName());
        }
        if (const auto saved = session.saveToStream(stream); saved.failed()) {
            return saved;
        }
        stream.flush();
        if (stream.getStatus().failed()) {
            return stream.getStatus();
        }
    }
    return temporary.overwriteTargetFileWithTemporary()
               ? juce::Result::ok()
               : juce::Result::fail("Could not replace " + file.getFullPathName());
}

juce::Result Autosave::restore(Session& session, const juce::File& file) {
    if (!file.existsAsFile()) {
        return juce::Result::fail("No autosave");
    }
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) {
        return juce::Result::fail("Could not read " + file.getFullPathName());
    }
    return session.loadFromStream(stream);
}
