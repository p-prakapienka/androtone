#pragma once

#include "Song.h"

#include <juce_data_structures/juce_data_structures.h>

// Natron's own song format: a versioned ValueTree, stored as XML.
//
//   <NatronSong version="1" tempo="120" masterVolume="0.8">
//     <Machine type="TestTone" name="Bass" volume="0.8" selectedPattern="0">
//       <Parameter id="level" value="0.2"/>
//       <Pattern index="0" bars="2">
//         <Note start="0" length="0.25" pitch="45" velocity="1"/>
//       </Pattern>
//     </Machine>
//   </NatronSong>
//
// Empty patterns (1 bar, no notes) are not written.
namespace SongFile {
    constexpr int currentVersion = 1;

    juce::ValueTree toValueTree(const Song& song);
    juce::Result fromValueTree(const juce::ValueTree& tree, Song& song);

    juce::Result save(const Song& song, juce::OutputStream& stream);
    juce::Result load(juce::InputStream& stream, Song& song);
}
