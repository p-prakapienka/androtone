#pragma once

#include "Song.h"

#include <juce_data_structures/juce_data_structures.h>

// Natron's own song format: a versioned ValueTree, stored as XML.
//
//   <NatronSong version="3" tempo="120" songMode="0" loop="0" loopStart="0" loopEnd="4">
//     <Master id="volume" value="0.8"/>
//     <Machine type="Acid" name="Bass" selectedPattern="0">
//       <Parameter id="cutoff" value="350"/>
//       <Mixer id="volume" value="0.8"/>
//       <Effect slot="0" type="Distortion">
//         <Parameter id="drive" value="18"/>
//       </Effect>
//       <Pattern index="0" bars="2">
//         <Note start="0" length="0.25" pitch="45" velocity="1"/>
//       </Pattern>
//       <Block start="0" length="4" pattern="0"/>
//     </Machine>
//   </NatronSong>
//
// Empty patterns (1 bar, no notes) are not written. Older versions are upgraded on load:
//   v1 → v2: Machine@volume becomes <Mixer id="volume">, NatronSong@masterVolume becomes
//            <Master id="volume">.
//   v2 → v3: nothing to convert; effects, blocks and the song/loop settings are new (defaults).
namespace SongFile {
    constexpr int currentVersion = 3;

    juce::ValueTree toValueTree(const Song& song);
    juce::Result fromValueTree(const juce::ValueTree& tree, Song& song);

    juce::Result save(const Song& song, juce::OutputStream& stream);
    juce::Result load(juce::InputStream& stream, Song& song);
}
