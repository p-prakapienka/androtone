namespace rb
{

DeviceSpec WaveSynth::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "wave";
    spec.displayName = "Wave";
    spec.params.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

} // namespace rb
