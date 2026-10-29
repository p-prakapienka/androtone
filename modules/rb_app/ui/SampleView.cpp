namespace rb
{

SampleView::SampleView (Studio& s, Navigator& n, int id) : studio (s), nav (n), trackId (id)
{
    studio.addListener (this);
}

SampleView::~SampleView()
{
    studio.removeListener (this);
}

void SampleView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.setColour (t.surface);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    const auto ref = track->isDrum() ? track->pads[(size_t) track->perf.selectedPad].sampleRef : track->samplerSampleRef;
    auto sample = studio.getDocument().getSample (ref);
    if (sample == nullptr)
    {
        g.setColour (t.textDim);
        g.drawText ("No sample", getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto area = getLocalBounds().toFloat().reduced (6.0f);
    const int frames = sample->getNumFrames();
    const int columns = juce::jmax (1, (int) area.getWidth());
    g.setColour (Theme::trackColour (track->colour));
    for (int x = 0; x < columns; ++x)
    {
        const int start = (int) ((juce::int64) frames * x / columns);
        const int end = juce::jmax (start + 1, (int) ((juce::int64) frames * (x + 1) / columns));
        const float peak = sample->buffer.getMagnitude (0, start, juce::jmin (end, frames) - start);
        const float h = peak * area.getHeight() * 0.5f;
        g.fillRect (area.getX() + x, area.getCentreY() - h, 1.0f, juce::jmax (1.0f, h * 2.0f));
    }
}

} // namespace rb
