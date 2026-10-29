namespace rb
{

DeviceSpec Reverb::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "reverb";
    spec.displayName = "Reverb";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
