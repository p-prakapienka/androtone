namespace rb
{

// STUB: placeholder content until the real presets land.
void addAnalogPresets (std::vector<TrackPreset>& presets)
{
    TrackPreset p;
    p.name = "Basic Bass";
    p.category = "Bass";
    p.type = InstrumentType::analog;
    presets.push_back (p);

    p.name = "Basic Keys";
    p.category = "Keys";
    presets.push_back (p);
}

} // namespace rb
