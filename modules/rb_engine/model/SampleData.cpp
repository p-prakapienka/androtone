namespace rb
{

SampleLibrary::SampleLibrary()
{
    formats.registerBasicFormats();
}

void SampleLibrary::setUserFolder (const juce::File& folder)
{
    userFolder = folder;
    userFolder.createDirectory();
}

juce::String SampleLibrary::cacheKey (const juce::String& ref, const juce::File& setFolder) const
{
    if (ref.startsWith ("set:"))
        return setFolder.getFullPathName() + "|" + ref;
    return ref;
}

SamplePtr SampleLibrary::get (const juce::String& ref, const juce::File& setFolder)
{
    if (ref.isEmpty())
        return nullptr;

    const auto key = cacheKey (ref, setFolder);
    {
        const std::lock_guard<std::mutex> lock (cacheMutex);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }

    SamplePtr sample;

    if (ref.startsWith ("factory:"))
        sample = FactorySounds::render (ref.fromFirstOccurrenceOf (":", false, false));
    else if (ref.startsWith ("set:"))
        sample = loadFile (setFolder.getChildFile ("samples").getChildFile (ref.fromFirstOccurrenceOf (":", false, false)), formats);
    else if (ref.startsWith ("user:"))
        sample = loadFile (userFolder.getChildFile (ref.fromFirstOccurrenceOf (":", false, false)), formats);

    if (sample == nullptr)
        return nullptr;

    // Another thread may have produced the same sample meanwhile: keep the first one, so
    // every user of a reference shares one buffer.
    const std::lock_guard<std::mutex> lock (cacheMutex);
    auto [it, inserted] = cache.emplace (key, sample);
    return it->second;
}

void SampleLibrary::put (const juce::String& ref, const juce::File& setFolder, SamplePtr sample)
{
    const std::lock_guard<std::mutex> lock (cacheMutex);
    cache[cacheKey (ref, setFolder)] = std::move (sample);
}

bool SampleLibrary::isCached (const juce::String& ref, const juce::File& setFolder) const
{
    const std::lock_guard<std::mutex> lock (cacheMutex);
    return cache.count (cacheKey (ref, setFolder)) > 0;
}

void SampleLibrary::warmUpFactorySounds (const std::function<bool()>& shouldStop)
{
    for (auto& info : FactorySounds::list())
    {
        if (shouldStop != nullptr && shouldStop())
            return;
        get ("factory:" + info.id, {});
    }
}

SamplePtr SampleLibrary::loadFile (const juce::File& file, juce::AudioFormatManager& formatManager)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    // Cap imports at five minutes; Note is a sketchpad, not an audio editor.
    const auto maxFrames = (juce::int64) (reader->sampleRate * 300.0);
    const int numFrames = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    auto sample = std::make_shared<SampleData>();
    sample->buffer.setSize (numChannels, numFrames);
    reader->read (&sample->buffer, 0, numFrames, 0, true, numChannels > 1);
    sample->sampleRate = reader->sampleRate;
    sample->name = file.getFileNameWithoutExtension();
    return sample;
}

bool SampleLibrary::writeWav (const SampleData& sample, const juce::File& file, int bitDepth)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sample.sampleRate)
                                                   .withNumChannels (sample.buffer.getNumChannels())
                                                   .withBitsPerSample (bitDepth));
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (sample.buffer, 0, sample.buffer.getNumSamples());
}

juce::StringArray SampleLibrary::listUserSamples() const
{
    juce::StringArray refs;
    if (! userFolder.isDirectory())
        return refs;

    for (auto& f : userFolder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
        refs.add ("user:" + f.getFileName());

    refs.sortNatural();
    return refs;
}

int findFirstTransient (const juce::AudioBuffer<float>& buffer, float thresholdDb)
{
    const float threshold = juce::Decibels::decibelsToGain (thresholdDb);
    const int numFrames = buffer.getNumSamples();

    // Relative to the loudest point, so quiet recordings still find their onset.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numFrames));

    if (peak <= 0.0f)
        return 0;

    const float level = juce::jmax (threshold * peak, 1.0e-4f);

    for (int i = 0; i < numFrames; ++i)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            if (std::abs (buffer.getSample (ch, i)) >= level)
                return juce::jmax (0, i - 32); // keep a few samples of attack

    return 0;
}

} // namespace rb
