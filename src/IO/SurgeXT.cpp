#include "SurgeXT.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#ifdef SWAPTUBE_USE_SURGE_XT
#include "ModulationSource.h"
#include "SurgeSynthesizer.h"
#include "SurgeStorage.h"
#include "dsp/SurgeVoice.h"
#endif

using namespace std;

namespace {

float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float clamp_bipolar(const float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

string lowercase_copy(string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

void require_range(const int index, const int count, const char* label) {
    if (index < 0 || index >= count) {
        throw runtime_error(string("Surge XT ") + label + " is out of range.");
    }
}

#ifdef SWAPTUBE_USE_SURGE_XT
double numeric_value_from_parameter(const Parameter& parameter, const pdata& value) {
    switch (parameter.valtype) {
        case vt_float:
            return value.f;
        case vt_int:
            return value.i;
        case vt_bool:
            return value.b ? 1.0 : 0.0;
    }

    return 0.0;
}

modsources require_mod_source_id(const int source_id) {
    require_range(source_id, n_modsources, "modulation source id");
    return static_cast<modsources>(source_id);
}
#endif

#ifdef SWAPTUBE_USE_SURGE_XT
modsources to_surge_mod_source(const SurgeXTModulationSource source) {
    switch (source) {
        case SurgeXTModulationSource::AmpEnvelope:
            return ms_ampeg;
        case SurgeXTModulationSource::FilterEnvelope:
            return ms_filtereg;
        case SurgeXTModulationSource::VoiceLFO1:
            return ms_lfo1;
        case SurgeXTModulationSource::VoiceLFO2:
            return ms_lfo2;
        case SurgeXTModulationSource::VoiceLFO3:
            return ms_lfo3;
        case SurgeXTModulationSource::VoiceLFO4:
            return ms_lfo4;
        case SurgeXTModulationSource::VoiceLFO5:
            return ms_lfo5;
        case SurgeXTModulationSource::VoiceLFO6:
            return ms_lfo6;
        case SurgeXTModulationSource::SceneLFO1:
            return ms_slfo1;
        case SurgeXTModulationSource::SceneLFO2:
            return ms_slfo2;
        case SurgeXTModulationSource::SceneLFO3:
            return ms_slfo3;
        case SurgeXTModulationSource::SceneLFO4:
            return ms_slfo4;
        case SurgeXTModulationSource::SceneLFO5:
            return ms_slfo5;
        case SurgeXTModulationSource::SceneLFO6:
            return ms_slfo6;
    }

    throw runtime_error("Unsupported Surge XT modulation source.");
}
#endif

}

struct SurgeXT::Impl {
#ifdef SWAPTUBE_USE_SURGE_XT
    struct PluginLayerProxy : public SurgeSynthesizer::PluginLayer {
        void surgeParameterUpdated(const SurgeSynthesizer::ID&, float) override {}
        void surgeMacroUpdated(long, float) override {}
    };

    unique_ptr<PluginLayerProxy> plugin_layer;
    shared_ptr<SurgeSynthesizer> synth;

    explicit Impl(const int sample_rate_hz) {
        plugin_layer = make_unique<PluginLayerProxy>();

        const string data_path = SWAPTUBE_SURGE_XT_DATA_DIR;
        const string supplied_path =
            data_path.empty() ? SurgeStorage::skipPatchLoadDataPathSentinel : data_path;

        synth = shared_ptr<SurgeSynthesizer>(new SurgeSynthesizer(plugin_layer.get(), supplied_path));
        synth->setSamplerate(static_cast<float>(sample_rate_hz));
        synth->time_data.tempo = 120;
        synth->time_data.ppqPos = 0;

        for (int i = 0; i < 4; ++i) {
            synth->process();
        }
    }

    bool has_parameter(const long parameter_id) const {
        return parameter_id >= 0 &&
               parameter_id < static_cast<long>(synth->storage.getPatch().param_ptr.size());
    }

    SurgeSynthesizer::ID require_id(const long parameter_id) const {
        SurgeSynthesizer::ID id;
        if (!synth->fromSynthSideId(static_cast<int>(parameter_id), id)) {
            throw runtime_error("Surge XT parameter id is out of range.");
        }
        return id;
    }

    Parameter* require_parameter(const long parameter_id) const {
        return synth->storage.getPatch().param_ptr.at(static_cast<size_t>(parameter_id));
    }

    string parameter_name(const long parameter_id) const {
        char buffer[512]{};
        synth->getParameterName(require_id(parameter_id), buffer);
        return buffer;
    }

    string parameter_display(const long parameter_id) const {
        return require_parameter(parameter_id)->get_display();
    }

    string parameter_display_for_normalized(const long parameter_id, const float normalized_01) const {
        auto* parameter = require_parameter(parameter_id);
        return parameter->get_display(true, parameter->normalized_to_value(clamp01(normalized_01)));
    }

    long parameter_id_for(const Parameter* parameter) const {
        return parameter->id;
    }

    void set_parameter_01(Parameter* parameter, const float normalized_01) {
        synth->setParameter01(synth->idForParameter(parameter), clamp01(normalized_01), true, false);
        synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
    }

    bool set_parameter_01(const long parameter_id, const float normalized_01, const bool force_integer) {
        const bool success =
            synth->setParameter01(require_id(parameter_id), clamp01(normalized_01), true, force_integer);
        synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
        return success;
    }
#else
    explicit Impl(const int) {
        throw runtime_error(SurgeXT::availability_message());
    }
#endif
};

SurgeXT::SurgeXT(const int sample_rate_hz) : impl(make_unique<Impl>(sample_rate_hz)) {}
SurgeXT::~SurgeXT() = default;
SurgeXT::SurgeXT(SurgeXT&&) noexcept = default;
SurgeXT& SurgeXT::operator=(SurgeXT&&) noexcept = default;

bool SurgeXT::available() {
#ifdef SWAPTUBE_USE_SURGE_XT
    return true;
#else
    return false;
#endif
}

string SurgeXT::availability_message() {
#ifdef SWAPTUBE_USE_SURGE_XT
    return "Surge XT is available.";
#else
    return "Surge XT support is disabled. Reconfigure with -DSWAPTUBE_ENABLE_SURGE_XT=ON "
           "-DSURGE_XT_SOURCE_DIR=/abs/path/to/surge";
#endif
}

vector<SurgeXTPatchInfo> SurgeXT::list_patches() const {
    vector<SurgeXTPatchInfo> patches;
#ifdef SWAPTUBE_USE_SURGE_XT
    if (!impl || !impl->synth) {
        return patches;
    }

    const auto& storage = impl->synth->storage;
    patches.reserve(storage.patchOrdering.empty() ? storage.patch_list.size() : storage.patchOrdering.size());

    if (storage.patchOrdering.empty()) {
        for (size_t i = 0; i < storage.patch_list.size(); ++i) {
            const auto& patch = storage.patch_list[i];
            const string category =
                patch.category >= 0 && patch.category < static_cast<int>(storage.patch_category.size())
                    ? storage.patch_category[patch.category].name
                    : "";
            patches.push_back({static_cast<int>(i), patch.name, category, patch.path.string()});
        }
        return patches;
    }

    for (size_t ordered_index = 0; ordered_index < storage.patchOrdering.size(); ++ordered_index) {
        const int patch_index = storage.patchOrdering[ordered_index];
        if (patch_index < 0 || patch_index >= static_cast<int>(storage.patch_list.size())) {
            continue;
        }
        const auto& patch = storage.patch_list[patch_index];
        const string category =
            patch.category >= 0 && patch.category < static_cast<int>(storage.patch_category.size())
                ? storage.patch_category[patch.category].name
                : "";
        patches.push_back({static_cast<int>(ordered_index), patch.name, category, patch.path.string()});
    }
#endif
    return patches;
}

vector<SurgeXTParameterInfo> SurgeXT::list_parameters() const {
    vector<SurgeXTParameterInfo> parameters;
#ifdef SWAPTUBE_USE_SURGE_XT
    if (!impl || !impl->synth) {
        return parameters;
    }

    const auto& param_ptrs = impl->synth->storage.getPatch().param_ptr;
    parameters.reserve(param_ptrs.size());
    for (const auto* parameter : param_ptrs) {
        parametermeta metadata{};
        impl->synth->getParameterMeta(impl->synth->idForParameter(parameter), metadata);

        SurgeXTParameterInfo info;
        info.id = parameter->id;
        info.name = parameter->get_name();
        info.full_name = parameter->get_full_name();
        info.storage_name = parameter->get_storage_name();
        info.ui_identifier = parameter->ui_identifier;
        info.osc_name = parameter->oscName;
        info.control_group =
            (parameter->ctrlgroup >= 0 && parameter->ctrlgroup < endCG)
                ? ControlGroupDisplay[parameter->ctrlgroup]
                : "";
        info.control_group_id = parameter->ctrlgroup;
        info.control_group_entry = parameter->ctrlgroup_entry;
        info.scene = parameter->scene;
        info.param_id_in_scene = parameter->param_id_in_scene;
        info.control_type = parameter->ctrltype;
        info.value_type = parameter->valtype;
        info.modulateable = parameter->modulateable;
        info.is_bipolar = parameter->is_bipolar();
        info.is_discrete = parameter->is_discrete_selection();
        info.can_temposync = parameter->can_temposync();
        info.can_extend_range = parameter->can_extend_range();
        info.can_deactivate = parameter->can_deactivate();
        info.can_be_absolute = parameter->can_be_absolute();
        info.can_be_nondestructively_modulated = parameter->can_be_nondestructively_modulated();
        info.hidden = metadata.hide;
        info.expert = metadata.expert;
        info.meta = metadata.meta;
        info.normalized_value = parameter->get_value_f01();
        info.default_normalized_value = parameter->get_default_value_f01();
        info.value = numeric_value_from_parameter(*parameter, parameter->val);
        info.min_value = numeric_value_from_parameter(*parameter, parameter->val_min);
        info.max_value = numeric_value_from_parameter(*parameter, parameter->val_max);
        info.default_value = numeric_value_from_parameter(*parameter, parameter->val_default);
        parameters.push_back(std::move(info));
    }
#endif
    return parameters;
}

optional<SurgeXTParameterInfo> SurgeXT::get_parameter_info(const long parameter_id) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    if (!impl || !impl->has_parameter(parameter_id)) {
        return nullopt;
    }

    const auto all_parameters = list_parameters();
    for (const auto& parameter : all_parameters) {
        if (parameter.id == parameter_id) {
            return parameter;
        }
    }
    return nullopt;
#else
    (void)parameter_id;
    return nullopt;
#endif
}

vector<long> SurgeXT::find_parameter_ids(const string& query) const {
    vector<long> ids;
#ifdef SWAPTUBE_USE_SURGE_XT
    const string lowered_query = lowercase_copy(query);
    if (lowered_query.empty()) {
        return ids;
    }

    const auto& param_ptrs = impl->synth->storage.getPatch().param_ptr;
    ids.reserve(param_ptrs.size());
    for (const auto* parameter : param_ptrs) {
        const string haystack =
            lowercase_copy(string(parameter->get_name()) + "\n" + parameter->get_full_name() + "\n" +
                           parameter->get_storage_name() + "\n" + parameter->ui_identifier + "\n" +
                           parameter->oscName);
        if (haystack.find(lowered_query) != string::npos) {
            ids.push_back(parameter->id);
        }
    }
#else
    (void)query;
#endif
    return ids;
}

vector<SurgeXTModulationSourceInfo> SurgeXT::list_modulation_sources() const {
    vector<SurgeXTModulationSourceInfo> sources;
#ifdef SWAPTUBE_USE_SURGE_XT
    sources.reserve(n_modsources);
    for (int source_id = 0; source_id < n_modsources; ++source_id) {
        const modsources source = static_cast<modsources>(source_id);
        sources.push_back({
            source_id,
            modsource_names_tag[source_id],
            modsource_names[source_id],
            modsource_names_button[source_id],
            isScenelevel(source),
            isEnvelope(source),
            isLFO(source),
            isCustomController(source),
            isVoiceModulator(source),
        });
    }
#endif
    return sources;
}

bool SurgeXT::has_parameter(const long parameter_id) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl && impl->has_parameter(parameter_id);
#else
    (void)parameter_id;
    return false;
#endif
}

void SurgeXT::load_patch(const int ordered_patch_index) {
#ifdef SWAPTUBE_USE_SURGE_XT
    impl->synth->loadPatch(ordered_patch_index);
    impl->synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
    for (int i = 0; i < 4; ++i) {
        impl->synth->process();
    }
#else
    (void)ordered_patch_index;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::load_patch_by_path(const string& fxp_path, const string& patch_name) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const bool loaded = impl->synth->loadPatchByPath(fxp_path.c_str(), -1, patch_name.c_str(), true);
    impl->synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
    for (int i = 0; i < 4; ++i) {
        impl->synth->process();
    }
    return loaded;
#else
    (void)fxp_path;
    (void)patch_name;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::note_on(const int midi_note, const int velocity, const int channel, const int note_id) {
#ifdef SWAPTUBE_USE_SURGE_XT
    impl->synth->playNote(static_cast<char>(channel), static_cast<char>(midi_note),
                          static_cast<char>(velocity), 0, note_id);
#else
    (void)midi_note;
    (void)velocity;
    (void)channel;
    (void)note_id;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::note_off(const int midi_note, const int velocity, const int channel, const int note_id) {
#ifdef SWAPTUBE_USE_SURGE_XT
    impl->synth->releaseNote(static_cast<char>(channel), static_cast<char>(midi_note),
                             static_cast<char>(velocity), note_id);
#else
    (void)midi_note;
    (void)velocity;
    (void)channel;
    (void)note_id;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::all_notes_off() {
#ifdef SWAPTUBE_USE_SURGE_XT
    impl->synth->allNotesOff();
#else
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_pitch_bend(const float normalized_bipolar, const int channel) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const int value = static_cast<int>(std::lround((clamp_bipolar(normalized_bipolar) + 1.0f) * 8192.0f));
    impl->synth->pitchBend(static_cast<char>(channel), std::max(0, std::min(16383, value)));
#else
    (void)normalized_bipolar;
    (void)channel;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_channel_cc(const int cc, const float normalized_01, const int channel) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const int value = static_cast<int>(std::lround(clamp01(normalized_01) * 127.0f));
    impl->synth->channelController(static_cast<char>(channel), cc, value);
#else
    (void)cc;
    (void)normalized_01;
    (void)channel;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_mod_wheel(const float normalized_01, const int channel) {
    set_channel_cc(1, normalized_01, channel);
}

void SurgeXT::set_aftertouch(const float normalized_01, const int channel) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const int value = static_cast<int>(std::lround(clamp01(normalized_01) * 127.0f));
    impl->synth->channelAftertouch(static_cast<char>(channel), value);
#else
    (void)normalized_01;
    (void)channel;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_macro(const int macro_index_1_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    if (macro_index_1_based < 1 || macro_index_1_based > 8) {
        throw runtime_error("Surge XT macro index must be in the range 1..8.");
    }
    impl->synth->setMacroParameter01(macro_index_1_based - 1, clamp01(normalized_01));
#else
    (void)macro_index_1_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_note_pitch(const int note_id,
                             const int held_midi_note,
                             const float midi_note,
                             const int channel) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const float clamped_midi_note = std::max(0.0f, std::min(127.0f, midi_note));
    const float detune_semitones = clamped_midi_note - static_cast<float>(held_midi_note);
    impl->synth->setNoteExpression(SurgeVoice::PITCH, note_id, -1,
                                   static_cast<int16_t>(channel), detune_semitones);
#else
    (void)note_id;
    (void)held_midi_note;
    (void)midi_note;
    (void)channel;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_parameter_01(const long parameter_id,
                               const float normalized_01,
                               const bool force_integer) {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->set_parameter_01(parameter_id, normalized_01, force_integer);
#else
    (void)parameter_id;
    (void)normalized_01;
    (void)force_integer;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_parameter_value(const long parameter_id,
                                  const float value,
                                  const bool force_integer) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const float normalized_01 = value_to_normalized(parameter_id, value);
    return impl->set_parameter_01(parameter_id, normalized_01, force_integer);
#else
    (void)parameter_id;
    (void)value;
    (void)force_integer;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_parameter_from_string(const long parameter_id,
                                        const string& value,
                                        string& error_message) {
#ifdef SWAPTUBE_USE_SURGE_XT
    const auto parameter_id_obj = impl->require_id(parameter_id);
    float normalized_01 = 0.0f;
    if (!impl->synth->stringToNormalizedValue(parameter_id_obj, value, normalized_01)) {
        error_message = "Surge XT could not parse that parameter value.";
        return false;
    }

    error_message.clear();
    return impl->set_parameter_01(parameter_id, normalized_01, false);
#else
    (void)parameter_id;
    (void)value;
    error_message = availability_message();
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_scene_volume(const int scene_index_0_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    impl->set_parameter_01(&impl->synth->storage.getPatch().scene[scene_index_0_based].volume,
                           normalized_01);
#else
    (void)scene_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_scene_pan(const int scene_index_0_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    impl->set_parameter_01(&impl->synth->storage.getPatch().scene[scene_index_0_based].pan,
                           normalized_01);
#else
    (void)scene_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_scene_width(const int scene_index_0_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    impl->set_parameter_01(&impl->synth->storage.getPatch().scene[scene_index_0_based].width,
                           normalized_01);
#else
    (void)scene_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_waveshaper_drive(const int scene_index_0_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    impl->set_parameter_01(&impl->synth->storage.getPatch().scene[scene_index_0_based].wsunit.drive,
                           normalized_01);
#else
    (void)scene_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_scene_send_level(const int scene_index_0_based,
                                   const int send_slot_0_based,
                                   const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(send_slot_0_based, n_send_slots, "send slot");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].send_level[send_slot_0_based],
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)send_slot_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_fx_return_level(const int fx_slot_0_based, const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(fx_slot_0_based, n_fx_slots, "FX slot");
    impl->set_parameter_01(&impl->synth->storage.getPatch().fx[fx_slot_0_based].return_level,
                           normalized_01);
#else
    (void)fx_slot_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_filter_cutoff(const int scene_index_0_based,
                                const int filter_index_0_based,
                                const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .cutoff,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_filter_resonance(const int scene_index_0_based,
                                   const int filter_index_0_based,
                                   const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .resonance,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_filter_env_amount(const int scene_index_0_based,
                                    const int filter_index_0_based,
                                    const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .envmod,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_envelope_attack(const int scene_index_0_based,
                                  const int envelope_index_0_based,
                                  const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(envelope_index_0_based, 2, "envelope index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].adsr[envelope_index_0_based].a,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)envelope_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_envelope_release(const int scene_index_0_based,
                                   const int envelope_index_0_based,
                                   const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(envelope_index_0_based, 2, "envelope index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].adsr[envelope_index_0_based].r,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)envelope_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_lfo_rate(const int scene_index_0_based,
                           const int lfo_index_0_based,
                           const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(lfo_index_0_based, n_lfos, "LFO index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].lfo[lfo_index_0_based].rate,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)lfo_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_lfo_depth(const int scene_index_0_based,
                            const int lfo_index_0_based,
                            const float normalized_01) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(lfo_index_0_based, n_lfos, "LFO index");
    impl->set_parameter_01(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].lfo[lfo_index_0_based].magnitude,
        normalized_01);
#else
    (void)scene_index_0_based;
    (void)lfo_index_0_based;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::set_scene_filter_cutoff(const int scene_index_0_based,
                                      const int filter_index_0_based,
                                      const float normalized_01) {
    set_filter_cutoff(scene_index_0_based, filter_index_0_based, normalized_01);
}

float SurgeXT::get_parameter_01(const long parameter_id) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->require_parameter(parameter_id)->get_value_f01();
#else
    (void)parameter_id;
    throw runtime_error(availability_message());
#endif
}

float SurgeXT::normalized_to_value(const long parameter_id, const float normalized_01) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->require_parameter(parameter_id)->normalized_to_value(clamp01(normalized_01));
#else
    (void)parameter_id;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

float SurgeXT::value_to_normalized(const long parameter_id, const float value) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->require_parameter(parameter_id)->value_to_normalized(value);
#else
    (void)parameter_id;
    (void)value;
    throw runtime_error(availability_message());
#endif
}

string SurgeXT::get_parameter_name(const long parameter_id) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->parameter_name(parameter_id);
#else
    (void)parameter_id;
    throw runtime_error(availability_message());
#endif
}

string SurgeXT::get_parameter_display(const long parameter_id) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->parameter_display(parameter_id);
#else
    (void)parameter_id;
    throw runtime_error(availability_message());
#endif
}

string SurgeXT::get_parameter_display_for_normalized(const long parameter_id,
                                                     const float normalized_01) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    return impl->parameter_display_for_normalized(parameter_id, normalized_01);
#else
    (void)parameter_id;
    (void)normalized_01;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::scene_volume_parameter_id(const int scene_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    return impl->parameter_id_for(&impl->synth->storage.getPatch().scene[scene_index_0_based].volume);
#else
    (void)scene_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::scene_pan_parameter_id(const int scene_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    return impl->parameter_id_for(&impl->synth->storage.getPatch().scene[scene_index_0_based].pan);
#else
    (void)scene_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::scene_width_parameter_id(const int scene_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    return impl->parameter_id_for(&impl->synth->storage.getPatch().scene[scene_index_0_based].width);
#else
    (void)scene_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::waveshaper_drive_parameter_id(const int scene_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    return impl->parameter_id_for(&impl->synth->storage.getPatch().scene[scene_index_0_based].wsunit.drive);
#else
    (void)scene_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::scene_send_level_parameter_id(const int scene_index_0_based,
                                            const int send_slot_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(send_slot_0_based, n_send_slots, "send slot");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].send_level[send_slot_0_based]);
#else
    (void)scene_index_0_based;
    (void)send_slot_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::fx_return_level_parameter_id(const int fx_slot_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(fx_slot_0_based, n_fx_slots, "FX slot");
    return impl->parameter_id_for(&impl->synth->storage.getPatch().fx[fx_slot_0_based].return_level);
#else
    (void)fx_slot_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::filter_cutoff_parameter_id(const int scene_index_0_based,
                                         const int filter_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .cutoff);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::filter_resonance_parameter_id(const int scene_index_0_based,
                                            const int filter_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .resonance);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::filter_env_amount_parameter_id(const int scene_index_0_based,
                                             const int filter_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(filter_index_0_based, n_filterunits_per_scene, "filter index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].filterunit[filter_index_0_based]
             .envmod);
#else
    (void)scene_index_0_based;
    (void)filter_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::envelope_attack_parameter_id(const int scene_index_0_based,
                                           const int envelope_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(envelope_index_0_based, 2, "envelope index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].adsr[envelope_index_0_based].a);
#else
    (void)scene_index_0_based;
    (void)envelope_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::envelope_release_parameter_id(const int scene_index_0_based,
                                            const int envelope_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(envelope_index_0_based, 2, "envelope index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].adsr[envelope_index_0_based].r);
#else
    (void)scene_index_0_based;
    (void)envelope_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::lfo_rate_parameter_id(const int scene_index_0_based,
                                    const int lfo_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(lfo_index_0_based, n_lfos, "LFO index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].lfo[lfo_index_0_based].rate);
#else
    (void)scene_index_0_based;
    (void)lfo_index_0_based;
    throw runtime_error(availability_message());
#endif
}

long SurgeXT::lfo_depth_parameter_id(const int scene_index_0_based,
                                     const int lfo_index_0_based) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(scene_index_0_based, n_scenes, "scene index");
    require_range(lfo_index_0_based, n_lfos, "LFO index");
    return impl->parameter_id_for(
        &impl->synth->storage.getPatch().scene[scene_index_0_based].lfo[lfo_index_0_based].magnitude);
#else
    (void)scene_index_0_based;
    (void)lfo_index_0_based;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_modulation_depth_01(const long destination_parameter_id,
                                      const SurgeXTModulationSource mod_source,
                                      const float normalized_01,
                                      const int source_scene_0_based,
                                      const int source_index) {
#ifdef SWAPTUBE_USE_SURGE_XT
    return set_modulation_depth_01(destination_parameter_id,
                                   static_cast<int>(to_surge_mod_source(mod_source)),
                                   normalized_01, source_scene_0_based, source_index);
#else
    (void)destination_parameter_id;
    (void)mod_source;
    (void)normalized_01;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_modulation_depth_01(const long destination_parameter_id,
                                      const int surge_mod_source_id,
                                      const float normalized_01,
                                      const int source_scene_0_based,
                                      const int source_index) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(source_scene_0_based, n_scenes, "source scene index");
    const bool success = impl->synth->setModDepth01(destination_parameter_id,
                                                    require_mod_source_id(surge_mod_source_id),
                                                    source_scene_0_based, source_index,
                                                    clamp01(normalized_01));
    impl->synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
    return success;
#else
    (void)destination_parameter_id;
    (void)surge_mod_source_id;
    (void)normalized_01;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_modulation_depth_bipolar(const long destination_parameter_id,
                                           const SurgeXTModulationSource mod_source,
                                           const float normalized_bipolar,
                                           const int source_scene_0_based,
                                           const int source_index) {
#ifdef SWAPTUBE_USE_SURGE_XT
    return set_modulation_depth_bipolar(destination_parameter_id,
                                        static_cast<int>(to_surge_mod_source(mod_source)),
                                        normalized_bipolar, source_scene_0_based, source_index);
#else
    (void)destination_parameter_id;
    (void)mod_source;
    (void)normalized_bipolar;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

bool SurgeXT::set_modulation_depth_bipolar(const long destination_parameter_id,
                                           const int surge_mod_source_id,
                                           const float normalized_bipolar,
                                           const int source_scene_0_based,
                                           const int source_index) {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(source_scene_0_based, n_scenes, "source scene index");
    const modsources surge_mod_source = require_mod_source_id(surge_mod_source_id);
    const float clamped_depth = clamp_bipolar(normalized_bipolar);
    const float magnitude = std::abs(clamped_depth);
    const bool success = impl->synth->setModDepth01(destination_parameter_id, surge_mod_source,
                                                    source_scene_0_based, source_index, magnitude);
    if (success && magnitude > 0.0f) {
        auto* routing = impl->synth->getModRouting(destination_parameter_id, surge_mod_source,
                                                   source_scene_0_based, source_index);
        if (routing) {
            const Parameter* parameter =
                impl->synth->storage.getPatch().param_ptr[destination_parameter_id];
            routing->depth = std::copysign(parameter->set_modulation_f01(magnitude), clamped_depth);
            impl->synth->storage.getPatch().isDirty = true;
        }
    }
    impl->synth->processAudioThreadOpsWhenAudioEngineUnavailable(true);
    return success;
#else
    (void)destination_parameter_id;
    (void)surge_mod_source_id;
    (void)normalized_bipolar;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

float SurgeXT::get_modulation_depth_01(const long destination_parameter_id,
                                       const int surge_mod_source_id,
                                       const int source_scene_0_based,
                                       const int source_index) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(source_scene_0_based, n_scenes, "source scene index");
    return impl->synth->getModDepth01(destination_parameter_id,
                                      require_mod_source_id(surge_mod_source_id),
                                      source_scene_0_based, source_index);
#else
    (void)destination_parameter_id;
    (void)surge_mod_source_id;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

float SurgeXT::get_modulation_depth_bipolar(const long destination_parameter_id,
                                            const int surge_mod_source_id,
                                            const int source_scene_0_based,
                                            const int source_index) const {
#ifdef SWAPTUBE_USE_SURGE_XT
    require_range(source_scene_0_based, n_scenes, "source scene index");
    return impl->require_parameter(destination_parameter_id)->get_modulation_f01(
        impl->synth->getModDepth(destination_parameter_id, require_mod_source_id(surge_mod_source_id),
                                 source_scene_0_based, source_index));
#else
    (void)destination_parameter_id;
    (void)surge_mod_source_id;
    (void)source_scene_0_based;
    (void)source_index;
    throw runtime_error(availability_message());
#endif
}

void SurgeXT::render_samples(const int num_samples, vector<sample_t>& left, vector<sample_t>& right) {
    if (num_samples < 0) {
        throw runtime_error("render_samples requires a non-negative sample count.");
    }

    left.clear();
    right.clear();
    left.reserve(num_samples);
    right.reserve(num_samples);

#ifdef SWAPTUBE_USE_SURGE_XT
    const int block_size = impl->synth->getBlockSize();
    int remaining = num_samples;
    while (remaining > 0) {
        impl->synth->process();
        const int write_count = std::min(block_size, remaining);
        for (int i = 0; i < write_count; ++i) {
            left.push_back(float_to_sample(impl->synth->output[0][i]));
            right.push_back(float_to_sample(impl->synth->output[1][i]));
        }
        remaining -= write_count;
    }
#else
    (void)num_samples;
    throw runtime_error(availability_message());
#endif
}
