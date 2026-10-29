namespace rb
{

DeviceSpec Phaser::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "phaser";
    spec.displayName = "Phaser";
    spec.params.push_back (ParamSpec::continuous ("mix", "Dry/Wet", 0.0f, 1.0f, 1.0f, ParamUnit::percent));
    return spec;
}

} // namespace rb
