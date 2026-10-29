namespace rb
{

DeviceSpec Delay::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "delay";
    spec.displayName = "Delay";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
