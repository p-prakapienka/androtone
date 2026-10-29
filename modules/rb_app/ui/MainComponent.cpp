namespace rb
{

MainComponent::MainComponent()
{
    engine.start (false);
    setSize (420, 860);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));
}

void MainComponent::resized() {}

} // namespace rb
