namespace rb
{

DeviceSpec Dynamics::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "dynamics";
    spec.displayName = "Dynamics";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
