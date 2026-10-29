namespace rb::FactorySounds
{

// STUB: no factory sounds yet.
const std::vector<Info>& list()
{
    static const std::vector<Info> sounds;
    return sounds;
}

SamplePtr render (const juce::String&)
{
    return nullptr;
}

} // namespace rb::FactorySounds
