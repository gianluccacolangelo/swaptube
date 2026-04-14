#pragma once

#include "AudioWriter.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SurgeXTPatchInfo {
    int ordered_index = -1;
    std::string name;
    std::string category;
    std::string path;
};

struct SurgeXTParameterInfo {
    long id = -1;
    std::string name;
    std::string full_name;
    std::string storage_name;
    std::string ui_identifier;
    std::string osc_name;
    std::string control_group;
    int control_group_id = 0;
    int control_group_entry = 0;
    int scene = 0;
    int param_id_in_scene = 0;
    int control_type = 0;
    int value_type = 0;
    bool modulateable = false;
    bool is_bipolar = false;
    bool is_discrete = false;
    bool can_temposync = false;
    bool can_extend_range = false;
    bool can_deactivate = false;
    bool can_be_absolute = false;
    bool can_be_nondestructively_modulated = false;
    bool hidden = false;
    bool expert = false;
    bool meta = false;
    float normalized_value = 0.0f;
    float default_normalized_value = 0.0f;
    double value = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    double default_value = 0.0;
};

struct SurgeXTModulationSourceInfo {
    int id = -1;
    std::string tag;
    std::string name;
    std::string short_name;
    bool is_scene_level = false;
    bool is_envelope = false;
    bool is_lfo = false;
    bool is_custom_controller = false;
    bool is_voice_modulator = false;
};

enum class SurgeXTModulationSource {
    AmpEnvelope,
    FilterEnvelope,
    VoiceLFO1,
    VoiceLFO2,
    VoiceLFO3,
    VoiceLFO4,
    VoiceLFO5,
    VoiceLFO6,
    SceneLFO1,
    SceneLFO2,
    SceneLFO3,
    SceneLFO4,
    SceneLFO5,
    SceneLFO6,
};

class SurgeXT {
public:
    explicit SurgeXT(int sample_rate_hz);
    ~SurgeXT();

    SurgeXT(const SurgeXT&) = delete;
    SurgeXT& operator=(const SurgeXT&) = delete;
    SurgeXT(SurgeXT&&) noexcept;
    SurgeXT& operator=(SurgeXT&&) noexcept;

    static bool available();
    static std::string availability_message();

    std::vector<SurgeXTPatchInfo> list_patches() const;
    std::vector<SurgeXTParameterInfo> list_parameters() const;
    std::optional<SurgeXTParameterInfo> get_parameter_info(long parameter_id) const;
    std::vector<long> find_parameter_ids(const std::string& query) const;
    std::vector<SurgeXTModulationSourceInfo> list_modulation_sources() const;
    bool has_parameter(long parameter_id) const;

    void load_patch(int ordered_patch_index);
    bool load_patch_by_path(const std::string& fxp_path, const std::string& patch_name = "Swaptube");

    void note_on(int midi_note, int velocity = 100, int channel = 0, int note_id = -1);
    void note_off(int midi_note, int velocity = 0, int channel = 0, int note_id = -1);
    void all_notes_off();

    void set_pitch_bend(float normalized_bipolar, int channel = 0);
    void set_channel_cc(int cc, float normalized_01, int channel = 0);
    void set_mod_wheel(float normalized_01, int channel = 0);
    void set_aftertouch(float normalized_01, int channel = 0);
    void set_macro(int macro_index_1_based, float normalized_01);
    void set_note_pitch(int note_id, int held_midi_note, float midi_note, int channel = -1);
    bool set_parameter_01(long parameter_id, float normalized_01, bool force_integer = false);
    bool set_parameter_value(long parameter_id, float value, bool force_integer = false);
    bool set_parameter_from_string(long parameter_id,
                                   const std::string& value,
                                   std::string& error_message);
    void set_scene_volume(int scene_index_0_based, float normalized_01);
    void set_scene_pan(int scene_index_0_based, float normalized_01);
    void set_scene_width(int scene_index_0_based, float normalized_01);
    void set_waveshaper_drive(int scene_index_0_based, float normalized_01);
    void set_scene_send_level(int scene_index_0_based, int send_slot_0_based, float normalized_01);
    void set_fx_return_level(int fx_slot_0_based, float normalized_01);
    void set_filter_cutoff(int scene_index_0_based, int filter_index_0_based, float normalized_01);
    void set_filter_resonance(int scene_index_0_based, int filter_index_0_based, float normalized_01);
    void set_filter_env_amount(int scene_index_0_based, int filter_index_0_based, float normalized_01);
    void set_envelope_attack(int scene_index_0_based, int envelope_index_0_based, float normalized_01);
    void set_envelope_release(int scene_index_0_based, int envelope_index_0_based, float normalized_01);
    void set_lfo_rate(int scene_index_0_based, int lfo_index_0_based, float normalized_01);
    void set_lfo_depth(int scene_index_0_based, int lfo_index_0_based, float normalized_01);
    void set_scene_filter_cutoff(int scene_index_0_based, int filter_index_0_based, float normalized_01);
    long scene_volume_parameter_id(int scene_index_0_based) const;
    long scene_pan_parameter_id(int scene_index_0_based) const;
    long scene_width_parameter_id(int scene_index_0_based) const;
    long waveshaper_drive_parameter_id(int scene_index_0_based) const;
    long scene_send_level_parameter_id(int scene_index_0_based, int send_slot_0_based) const;
    long fx_return_level_parameter_id(int fx_slot_0_based) const;
    long filter_cutoff_parameter_id(int scene_index_0_based, int filter_index_0_based) const;
    long filter_resonance_parameter_id(int scene_index_0_based, int filter_index_0_based) const;
    long filter_env_amount_parameter_id(int scene_index_0_based, int filter_index_0_based) const;
    long envelope_attack_parameter_id(int scene_index_0_based, int envelope_index_0_based) const;
    long envelope_release_parameter_id(int scene_index_0_based, int envelope_index_0_based) const;
    long lfo_rate_parameter_id(int scene_index_0_based, int lfo_index_0_based) const;
    long lfo_depth_parameter_id(int scene_index_0_based, int lfo_index_0_based) const;
    float get_parameter_01(long parameter_id) const;
    float normalized_to_value(long parameter_id, float normalized_01) const;
    float value_to_normalized(long parameter_id, float value) const;
    std::string get_parameter_name(long parameter_id) const;
    std::string get_parameter_display(long parameter_id) const;
    std::string get_parameter_display_for_normalized(long parameter_id, float normalized_01) const;
    bool set_modulation_depth_01(long destination_parameter_id,
                                 SurgeXTModulationSource mod_source,
                                 float normalized_01,
                                 int source_scene_0_based = 0,
                                 int source_index = 0);
    bool set_modulation_depth_01(long destination_parameter_id,
                                 int surge_mod_source_id,
                                 float normalized_01,
                                 int source_scene_0_based = 0,
                                 int source_index = 0);
    bool set_modulation_depth_bipolar(long destination_parameter_id,
                                      SurgeXTModulationSource mod_source,
                                      float normalized_bipolar,
                                      int source_scene_0_based = 0,
                                      int source_index = 0);
    bool set_modulation_depth_bipolar(long destination_parameter_id,
                                      int surge_mod_source_id,
                                      float normalized_bipolar,
                                      int source_scene_0_based = 0,
                                      int source_index = 0);
    float get_modulation_depth_01(long destination_parameter_id,
                                  int surge_mod_source_id,
                                  int source_scene_0_based = 0,
                                  int source_index = 0) const;
    float get_modulation_depth_bipolar(long destination_parameter_id,
                                       int surge_mod_source_id,
                                       int source_scene_0_based = 0,
                                       int source_index = 0) const;

    void render_samples(int num_samples, std::vector<sample_t>& left, std::vector<sample_t>& right);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
