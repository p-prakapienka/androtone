#include "SampleData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace {
    const juce::Identifier sampleType { "Sample" };
    const juce::Identifier nameId { "name" };
    const juce::Identifier sampleRateId { "sampleRate" };
    const juce::Identifier wavId { "wav" };

    juce::Result read(std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name, SampleData& sample) {
        if (reader == nullptr) {
            return juce::Result::fail("Unsupported or unreadable audio file");
        }
        if (reader->lengthInSamples <= 0 || reader->numChannels == 0) {
            return juce::Result::fail("The audio file is empty");
        }
        const auto maxLength = static_cast<juce::int64>(SampleData::maxSeconds * reader->sampleRate);
        const int length = static_cast<int>(juce::jmin(reader->lengthInSamples, maxLength));
        const int channels = juce::jmin(2, static_cast<int>(reader->numChannels));

        SampleData loaded;
        loaded.name = name;
        loaded.sampleRate = reader->sampleRate;
        loaded.audio.setSize(channels, length);
        if (!reader->read(&loaded.audio, 0, length, 0, true, channels > 1)) {
            return juce::Result::fail("Could not read the audio data");
        }
        sample = std::move(loaded);
        return juce::Result::ok();
    }
}

juce::Result SampleData::load(juce::InputStream& stream, const juce::String& name, SampleData& sample) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    // The manager wants to own the stream; read it into memory first so the caller keeps theirs.
    juce::MemoryBlock bytes;
    stream.readIntoMemoryBlock(bytes);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes, false)));
    return read(std::move(reader), name, sample);
}

juce::ValueTree SampleData::toTree() const {
    {
        juce::WavAudioFormat wav;
        // The writer owns (and flushes) its stream, so write into a block it can't outlive.
        juce::MemoryBlock block;
        std::unique_ptr<juce::OutputStream> blockStream = std::make_unique<juce::MemoryOutputStream>(block, false);
        if (auto writer = wav.createWriterFor(blockStream, juce::AudioFormatWriterOptions {}
                                                               .withSampleRate(sampleRate)
                                                               .withNumChannels(audio.getNumChannels())
                                                               .withBitsPerSample(16))) {
            writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
        }
        juce::ValueTree tree(sampleType);
        tree.setProperty(nameId, name, nullptr);
        tree.setProperty(sampleRateId, sampleRate, nullptr);
        tree.setProperty(wavId, block.toBase64Encoding(), nullptr);
        return tree;
    }
}

juce::Result SampleData::fromTree(const juce::ValueTree& tree, SampleData& sample) {
    if (!tree.hasType(sampleType)) {
        return juce::Result::fail("Not a sample");
    }
    juce::MemoryBlock bytes;
    if (!bytes.fromBase64Encoding(tree.getProperty(wavId).toString())) {
        return juce::Result::fail("Sample data is corrupt");
    }
    juce::WavAudioFormat wav;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        wav.createReaderFor(new juce::MemoryInputStream(bytes, true), true));
    return read(std::move(reader), tree.getProperty(nameId).toString(), sample);
}
