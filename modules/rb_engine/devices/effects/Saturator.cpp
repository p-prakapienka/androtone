namespace rb
{

DeviceSpec Saturator::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "saturator";
    spec.displayName = "Saturator";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
