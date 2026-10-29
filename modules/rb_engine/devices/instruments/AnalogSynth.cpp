namespace rb
{

DeviceSpec AnalogSynth::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "analog";
    spec.displayName = "Analog";
    spec.params.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

} // namespace rb
