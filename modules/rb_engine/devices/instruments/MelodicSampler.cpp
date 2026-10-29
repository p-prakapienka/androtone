namespace rb
{

DeviceSpec MelodicSampler::createSpec()
{
    DeviceSpec spec;
    spec.typeId = "sampler";
    spec.displayName = "Sampler";
    spec.params.push_back (ParamSpec::continuous ("volume", "Volume", -70.0f, 6.0f, 0.0f, ParamUnit::decibels));
    return spec;
}

} // namespace rb
