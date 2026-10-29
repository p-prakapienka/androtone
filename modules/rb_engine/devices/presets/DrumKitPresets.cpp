namespace rb
{

// STUB: placeholder content until the real presets land.
void addDrumKitPresets (std::vector<TrackPreset>& presets)
{
    TrackPreset p;
    p.name = "Basic Kit";
    p.category = "Drums";
    p.type = InstrumentType::drumKit;
    presets.push_back (p);
}

} // namespace rb
