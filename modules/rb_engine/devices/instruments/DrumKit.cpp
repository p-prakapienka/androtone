namespace rb
{

DeviceSpec DrumKit::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "drumKit";
    spec.displayName = "Drum Kit";
    spec.params.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

DeviceSpec DrumKit::createPadSpec()
{
    DeviceSpec spec;
    spec.typeId = "pad";
    spec.displayName = "Pad";
    spec.params.push_back (ParamSpec::continuous ("transpose", "Transpose", -48.0f, 48.0f, 0.0f, ParamUnit::semitones));
    spec.params.push_back (ParamSpec::continuous ("send", "Send", 0.0f, 1.0f, 0.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
