namespace rb
{

DeviceSpec Chorus::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "chorus";
    spec.displayName = "Chorus";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
