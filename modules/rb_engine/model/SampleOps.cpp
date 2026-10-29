namespace rb
{

//==============================================================================
WaveformPeaks::WaveformPeaks (SamplePtr s, int framesPerBucket)
    : sample (std::move (s)), bucketSize (juce::jmax (1, framesPerBucket))
{
    if (sample == nullptr || sample->buffer.getNumChannels() == 0)
        return;

    numFrames = sample->getNumFrames();
    const int numBuckets = (numFrames + bucketSize - 1) / bucketSize;
    buckets.reserve ((size_t) numBuckets);

    for (int b = 0; b < numBuckets; ++b)
        buckets.push_back (scanFrames (b * bucketSize, juce::jmin (numFrames, (b + 1) * bucketSize)));
}

WaveformPeaks::Range WaveformPeaks::scanFrames (int start, int end) const
{
    Range r;
    if (sample == nullptr || end <= start)
        return r;

    bool first = true;
    for (int ch = 0; ch < sample->buffer.getNumChannels(); ++ch)
    {
        auto range = juce::FloatVectorOperations::findMinAndMax (sample->buffer.getReadPointer (ch, start), end - start);
        if (first || range.getStart() < r.min) r.min = range.getStart();
        if (first || range.getEnd() > r.max)   r.max = range.getEnd();
        first = false;
    }
    return r;
}

std::vector<WaveformPeaks::Range> WaveformPeaks::getColumns (double startFrame, double endFrame, int numColumns) const
{
    std::vector<Range> columns ((size_t) juce::jmax (0, numColumns));
    if (numFrames == 0 || numColumns <= 0 || endFrame <= startFrame)
        return columns;

    const double perColumn = (endFrame - startFrame) / numColumns;

    for (int c = 0; c < numColumns; ++c)
    {
        const int from = juce::jlimit (0, numFrames, (int) std::floor (startFrame + c * perColumn));
        int to = juce::jlimit (0, numFrames, (int) std::floor (startFrame + (c + 1) * perColumn));
        if (to <= from)
        {
            // Zoomed in past one frame per column: show the frame under the column.
            if (from >= numFrames || startFrame + c * perColumn < 0.0)
                continue;
            to = from + 1;
        }

        if (to - from < bucketSize * 2)
        {
            columns[(size_t) c] = scanFrames (from, to);
            continue;
        }

        const int b0 = from / bucketSize;
        const int b1 = juce::jmin ((int) buckets.size(), (to + bucketSize - 1) / bucketSize);
        Range r = buckets[(size_t) b0];
        for (int b = b0 + 1; b < b1; ++b)
        {
            r.min = juce::jmin (r.min, buckets[(size_t) b].min);
            r.max = juce::jmax (r.max, buckets[(size_t) b].max);
        }
        columns[(size_t) c] = r;
    }

    return columns;
}

//==============================================================================
RecordingCleanupResult cleanUpRecording (const juce::AudioBuffer<float>& raw, double sampleRate, const juce::String& name)
{
    RecordingCleanupResult result;
    const int numFrames = raw.getNumSamples();
    const int numChannels = raw.getNumChannels();
    if (numFrames == 0 || numChannels == 0 || sampleRate <= 0.0)
        return result;

    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        peak = juce::jmax (peak, raw.getMagnitude (ch, 0, numFrames));

    // Below -90 dBFS there is nothing to keep (no input, muted microphone).
    if (peak < juce::Decibels::decibelsToGain (-90.0f))
        return result;

    // Start: first transient (it keeps a few frames of attack before the onset).
    const int start = juce::jlimit (0, numFrames - 1, findFirstTransient (raw));

    // End: last frame above -50 dB relative to the peak, plus a short tail.
    const float tailLevel = juce::jmax (peak * juce::Decibels::decibelsToGain (-50.0f), 1.0e-5f);
    int last = start;
    for (int i = numFrames - 1; i > start; --i)
    {
        bool loud = false;
        for (int ch = 0; ch < numChannels && ! loud; ++ch)
            loud = std::abs (raw.getSample (ch, i)) >= tailLevel;
        if (loud)
        {
            last = i;
            break;
        }
    }
    const int end = juce::jmin (numFrames, last + 1 + (int) (sampleRate * 0.05));
    const int length = juce::jmax (1, end - start);

    auto data = std::make_shared<SampleData>();
    data->sampleRate = sampleRate;
    data->name = name;
    data->buffer.setSize (numChannels, length);
    for (int ch = 0; ch < numChannels; ++ch)
        data->buffer.copyFrom (ch, 0, raw, ch, start, length);

    // Light normalisation of quiet takes.
    float trimmedPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        trimmedPeak = juce::jmax (trimmedPeak, data->buffer.getMagnitude (ch, 0, length));

    if (trimmedPeak > 0.0f && trimmedPeak < juce::Decibels::decibelsToGain (-18.0f))
    {
        const float gainDb = juce::jmin (24.0f, -6.0f - juce::Decibels::gainToDecibels (trimmedPeak));
        data->buffer.applyGain (juce::Decibels::decibelsToGain (gainDb));
        result.gainDb = gainDb;
    }

    // Fades at the cut points keep clicks out.
    if (start > 0)
    {
        const int fadeIn = juce::jmin (32, start, length);
        data->buffer.applyGainRamp (0, fadeIn, 0.0f, 1.0f);
    }
    if (end < numFrames)
    {
        const int fadeOut = juce::jmin (length, (int) (sampleRate * 0.01));
        data->buffer.applyGainRamp (length - fadeOut, fadeOut, 1.0f, 0.0f);
    }

    result.sample = data;
    result.trimmedStart = start;
    result.trimmedEnd = numFrames - (start + length);
    return result;
}

//==============================================================================
bool matchesSearch (const juce::String& text, const juce::String& query)
{
    const auto words = juce::StringArray::fromTokens (query, false);
    for (auto& w : words)
        if (w.isNotEmpty() && ! text.containsIgnoreCase (w))
            return false;
    return true;
}

std::vector<int> filterPresets (const std::vector<TrackPreset>& presets, const juce::String& category,
                                const juce::String& search, int type)
{
    std::vector<int> result;
    const bool searching = search.trim().isNotEmpty();

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        auto& p = presets[(size_t) i];
        if (type >= 0 && (int) p.type != type)
            continue;
        if (searching ? ! matchesSearch (p.name, search)
                      : (category.isNotEmpty() && p.category != category))
            continue;
        result.push_back (i);
    }
    return result;
}

juce::String sampleDisplayName (const juce::String& ref)
{
    if (ref.isEmpty())
        return {};

    const auto body = ref.fromFirstOccurrenceOf (":", false, false);
    if (ref.startsWith ("factory:"))
    {
        for (auto& info : FactorySounds::list())
            if (info.id == body)
                return info.name;
        return body;
    }

    return body.upToLastOccurrenceOf (".", false, false).isNotEmpty() ? body.upToLastOccurrenceOf (".", false, false)
                                                                       : body;
}

juce::StringArray listSetSamples (const juce::File& setFolder)
{
    juce::StringArray refs;
    const auto folder = setFolder.getChildFile ("samples");
    if (setFolder == juce::File() || ! folder.isDirectory())
        return refs;

    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
        refs.add ("set:" + f.getFileName());

    refs.sortNatural();
    return refs;
}

bool isSetRecording (const juce::String& ref)
{
    return ref.startsWith ("set:rec-");
}

juce::File makeRecordingFile (const juce::File& folder, juce::Time time)
{
    const auto base = "rec-" + time.formatted ("%Y%m%d-%H%M%S");
    auto file = folder.getChildFile (base + ".wav");
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile (base + "-" + juce::String (n) + ".wav");
    return file;
}

int startFrameFor (float startFraction, int numFrames)
{
    if (numFrames <= 0)
        return 0;
    return juce::jlimit (0, numFrames - 1, (int) std::floor (juce::jlimit (0.0f, 1.0f, startFraction) * (float) numFrames));
}

} // namespace rb
