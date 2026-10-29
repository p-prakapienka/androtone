namespace rb
{

namespace
{
    struct OfflineSession
    {
        OfflineSession (const Set& set, const juce::File& folder, SampleLibrary& library, double sampleRate)
            : document (session, library)
        {
            session.setOffline (true);
            session.prepare (sampleRate, blockSize);
            document.openInMemory (set, folder);
        }

        ~OfflineSession() { session.releaseResources(); }

        void render (juce::AudioBuffer<float>& out, int start, int numSamples)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            int done = 0;
            while (done < numSamples)
            {
                const int n = juce::jmin (blockSize, numSamples - done);
                session.process (block, n);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, start + done, block, ch, 0, n);
                done += n;
            }
        }

        static constexpr int blockSize = 512;
        SessionProcessor session;
        Document document;
    };

    int beatsToSamples (double beats, double tempo, double sampleRate)
    {
        return (int) std::llround (beats * 60.0 / tempo * sampleRate);
    }
}

juce::AudioBuffer<float> OfflineRenderer::renderScenes (const Set& set, const juce::File& setFolder,
                                                        SampleLibrary& library, double sampleRate,
                                                        double tailSeconds, Progress progress)
{
    std::vector<std::pair<int, int>> scenes; // scene, length in samples
    juce::int64 total = 0;

    for (int scene = 0; scene < numScenes; ++scene)
    {
        const double beats = set.sceneLengthBeats (scene);
        if (beats > 0.0)
        {
            const int samples = beatsToSamples (beats, set.tempo, sampleRate);
            scenes.emplace_back (scene, samples);
            total += samples;
        }
    }

    const int tail = (int) (tailSeconds * sampleRate);
    juce::AudioBuffer<float> out (2, (int) total + tail);
    out.clear();

    if (scenes.empty())
        return {};

    OfflineSession offline (set, setFolder, library, sampleRate);
    int position = 0;

    for (auto [scene, length] : scenes)
    {
        Command launch;
        launch.type = Command::Type::launchScene;
        launch.slot = scene;
        offline.session.post (launch);

        // Render in slices so progress can be reported and export cancelled.
        const int slice = (int) sampleRate;
        for (int done = 0; done < length; done += slice)
        {
            offline.render (out, position + done, juce::jmin (slice, length - done));
            if (progress != nullptr && ! progress ((float) (position + done) / (float) out.getNumSamples()))
                return {};
        }

        position += length;

        Command stop;
        stop.type = Command::Type::stop;
        offline.session.post (stop);
    }

    offline.render (out, position, tail);
    if (progress != nullptr)
        progress (1.0f);
    return out;
}

juce::AudioBuffer<float> OfflineRenderer::renderScene (const Set& set, const juce::File& setFolder,
                                                       SampleLibrary& library, int scene, double beats,
                                                       double sampleRate)
{
    const int length = beatsToSamples (beats, set.tempo, sampleRate);
    juce::AudioBuffer<float> out (2, length);
    out.clear();

    OfflineSession offline (set, setFolder, library, sampleRate);

    Command launch;
    launch.type = Command::Type::launchScene;
    launch.slot = scene;
    offline.session.post (launch);
    offline.render (out, 0, length);
    return out;
}

bool OfflineRenderer::write (const juce::AudioBuffer<float>& buffer, double sampleRate, Format format,
                             const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::AudioFormat> audioFormat;
    int bits = 16;

    switch (format)
    {
        case Format::wav16: audioFormat = std::make_unique<juce::WavAudioFormat>(); bits = 16; break;
        case Format::wav24: audioFormat = std::make_unique<juce::WavAudioFormat>(); bits = 24; break;
        case Format::flac:  audioFormat = std::make_unique<juce::FlacAudioFormat>(); bits = 16; break;
    }

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    if (stream == nullptr)
        return false;

    auto writer = audioFormat->createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                            .withNumChannels (2).withBitsPerSample (bits));
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

} // namespace rb
