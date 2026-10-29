#pragma once

#include "Song.h"

#include <juce_data_structures/juce_data_structures.h>

// Natron's own song format: a versioned ValueTree, stored as XML.
//
//   <NatronSong version="2" tempo="120">
//     <Master id="volume" value="0.8"/>
//     <Machine type="Acid" name="Bass" selectedPattern="0">
//       <Parameter id="cutoff" value="350"/>
//       <Mixer id="volume" value="0.8"/>
//       <Pattern index="0" bars="2">
//         <Note start="0" length="0.25" pitch="45" velocity="1"/>
//       </Pattern>
//     </Machine>
//   </NatronSong>
//
// Empty patterns (1 bar, no notes) are not written. Older versions are upgraded on load:
//   v1 → v2: Machine@volume becomes <Mixer id="volume">, NatronSong@masterVolume becomes
//            <Master id="volume">.
namespace SongFile {
    constexpr int currentVersion = 2;

    juce::ValueTree toValueTree(const Song& song);
    juce::Result fromValueTree(const juce::ValueTree& tree, Song& song);

    juce::Result save(const Song& song, juce::OutputStream& stream);
    juce::Result load(juce::InputStream& stream, Song& song);
}
