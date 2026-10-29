namespace rb
{

DeviceSpec AutoFilter::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "autoFilter";
    spec.displayName = "Auto Filter";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
