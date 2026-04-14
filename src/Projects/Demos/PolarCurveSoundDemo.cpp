#include "../Scenes/Scene.h"
#include "../Core/Smoketest.h"
#include "../IO/AudioWriter.h"
#include "../IO/SurgeXT.h"
#include "../IO/Writer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFullTurn = 2.0 * kPi;
constexpr double kDurationSeconds = 8.0;
constexpr int kMicroblocks = 16;
constexpr int kCurveSamples = 2048;
constexpr float kMinMidiNote = 48.0f;
constexpr float kMaxMidiNote = 72.0f;
constexpr float kHeldMidiNote = 60.0f;
constexpr float kMinCutoff = 0.12f;
constexpr float kMaxCutoff = 0.84f;
constexpr int kSurgeNoteId = 7001;
constexpr int kSurgeVelocity = 104;
constexpr int kControlBlockSamples = 256;
constexpr double kFadeSeconds = 0.03;

double radius_from_theta(const double theta) {
    return std::cos(theta);
}

double normalized_radius_from_theta(const double theta) {
    return std::clamp(0.5 * (radius_from_theta(theta) + 1.0), 0.0, 1.0);
}

vec2 raw_polar_curve_point(const double theta) {
    const double radius = radius_from_theta(theta);
    return vec2(radius * std::cos(theta), radius * std::sin(theta));
}

float midi_note_from_radius(const double radius) {
    const double normalized_radius = std::clamp(0.5 * (radius + 1.0), 0.0, 1.0);
    return kMinMidiNote + static_cast<float>(normalized_radius) * (kMaxMidiNote - kMinMidiNote);
}

float cutoff_from_radius(const double radius) {
    const double normalized_radius = std::clamp(0.5 * (radius + 1.0), 0.0, 1.0);
    return kMinCutoff + static_cast<float>(normalized_radius) * (kMaxCutoff - kMinCutoff);
}

double frequency_from_midi_note(const float midi_note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midi_note) - 69.0) / 12.0);
}

int color_from_frequency(const double frequency_hz) {
    const double min_frequency_hz = frequency_from_midi_note(kMinMidiNote);
    const double max_frequency_hz = frequency_from_midi_note(kMaxMidiNote);
    const double normalized = std::clamp((frequency_hz - min_frequency_hz) / (max_frequency_hz - min_frequency_hz), 0.0, 1.0);
    return HSVtoRGB(0.62 - normalized * 0.54, 0.85, 1.0);
}

void apply_edge_fade(std::vector<sample_t>& channel) {
    const int samplerate = get_audio_samplerate_hz();
    const int fade_samples = std::max(1, static_cast<int>(kFadeSeconds * samplerate));
    const int total_samples = static_cast<int>(channel.size());
    for (int i = 0; i < total_samples; ++i) {
        const int distance_to_edge = std::min(i, total_samples - 1 - i);
        const float fade = std::min(1.0f, static_cast<float>(distance_to_edge) / fade_samples);
        channel[i] = float_to_sample(sample_to_float(channel[i]) * fade);
    }
}

void generate_fallback_audio(std::vector<sample_t>& left, std::vector<sample_t>& right) {
    const int samplerate = get_audio_samplerate_hz();
    const int total_samples = static_cast<int>(kDurationSeconds * samplerate);

    left.reserve(total_samples);
    right.reserve(total_samples);

    double phase = 0.0;
    for (int i = 0; i < total_samples; ++i) {
        const double progress = total_samples > 1 ? static_cast<double>(i) / (total_samples - 1) : 0.0;
        const double theta = progress * kFullTurn;
        const double radius = radius_from_theta(theta);
        const double frequency_hz = frequency_from_midi_note(midi_note_from_radius(radius));

        phase += kFullTurn * frequency_hz / samplerate;

        const double amplitude = 0.12;
        const sample_t sample = float_to_sample(amplitude * std::sin(phase));

        left.push_back(sample);
        right.push_back(sample);
    }

    apply_edge_fade(left);
    apply_edge_fade(right);
}

const SurgeXTPatchInfo* find_still_pad_patch(const std::vector<SurgeXTPatchInfo>& patches) {
    for (const auto& patch : patches) {
        if (patch.category == "Pads" && patch.name == "Still") {
            return &patch;
        }
    }
    for (const auto& patch : patches) {
        if (patch.path.find("/Pads/Still.fxp") != std::string::npos) {
            return &patch;
        }
    }
    return nullptr;
}

void set_curve_filter_cutoff(SurgeXT& surge, const float cutoff_01) {
    for (int scene_index = 0; scene_index < 2; ++scene_index) {
        for (int filter_index = 0; filter_index < 2; ++filter_index) {
            surge.set_filter_cutoff(scene_index, filter_index, cutoff_01);
        }
    }
}

void generate_surge_audio(std::vector<sample_t>& left, std::vector<sample_t>& right) {
    if (!SurgeXT::available()) {
        generate_fallback_audio(left, right);
        return;
    }

    SurgeXT surge(get_audio_samplerate_hz());
    const auto patches = surge.list_patches();
    const auto* patch = find_still_pad_patch(patches);
    if (!patch) {
        throw std::runtime_error("Could not find the Surge XT factory patch Pads/Still.");
    }
    if (!surge.load_patch_by_path(patch->path, patch->name)) {
        throw std::runtime_error("Failed to load the Surge XT factory patch Pads/Still.");
    }

    const int samplerate = get_audio_samplerate_hz();
    const int total_samples = static_cast<int>(kDurationSeconds * samplerate);
    left.clear();
    right.clear();
    left.reserve(total_samples);
    right.reserve(total_samples);

    surge.note_on(static_cast<int>(kHeldMidiNote), kSurgeVelocity, 0, kSurgeNoteId);

    std::vector<sample_t> block_left;
    std::vector<sample_t> block_right;
    int rendered_samples = 0;
    while (rendered_samples < total_samples) {
        const int block_samples = std::min(kControlBlockSamples, total_samples - rendered_samples);
        const double sample_center = rendered_samples + 0.5 * block_samples;
        const double progress = total_samples > 1 ? sample_center / (total_samples - 1) : 0.0;
        const double theta = progress * kFullTurn;
        const double radius = radius_from_theta(theta);

        surge.set_note_pitch(kSurgeNoteId, static_cast<int>(kHeldMidiNote), midi_note_from_radius(radius));
        set_curve_filter_cutoff(surge, cutoff_from_radius(radius));
        surge.render_samples(block_samples, block_left, block_right);

        left.insert(left.end(), block_left.begin(), block_left.end());
        right.insert(right.end(), block_right.begin(), block_right.end());
        rendered_samples += block_samples;
    }

    surge.note_off(static_cast<int>(kHeldMidiNote), 0, 0, kSurgeNoteId);
    apply_edge_fade(left);
    apply_edge_fade(right);
}

class PolarCurveScene : public Scene {
public:
    PolarCurveScene(const vec2& dimensions = vec2(1, 1))
        : Scene(dimensions) {
        manager.set({
            {"theta", "0"},
            {"voice", "0"},
        });
    }

    void draw() override {
        draw_polar_grid();
        draw_reference_curve();
        draw_active_curve();
        draw_current_radius();
    }

    const StateQuery populate_state_query() const override { return StateQuery{"theta", "voice"}; }
    void change_data() override {}
    void mark_data_unchanged() override {}
    bool check_if_data_changed() const override { return false; }

private:
    vec2 frame_center() const {
        return vec2(get_width() * 0.5, get_height() * 0.5);
    }

    double max_curve_radius_pixels() const {
        const double min_dimension = std::min(get_width(), get_height());
        const double padding = min_dimension * 0.10;
        const double marker_allowance = min_dimension * 0.07;
        return std::max(12.0, min_dimension * 0.5 - padding - marker_allowance);
    }

    vec2 point_to_pixel(const vec2& point) const {
        return frame_center() + point * static_cast<float>(max_curve_radius_pixels());
    }

    void draw_polar_grid() {
        const vec2 origin = frame_center();
        const int spoke_color = argb(255, 70, 88, 112);
        const int ring_color = argb(255, 55, 70, 94);
        const int spoke_thickness = std::max(1, static_cast<int>(get_geom_mean_size() / 360.0));
        const double outer_radius_pixels = max_curve_radius_pixels();

        for (int i = 0; i < 8; ++i) {
            const double theta = kFullTurn * i / 8.0;
            const vec2 endpoint = origin + vec2(std::cos(theta) * outer_radius_pixels, std::sin(theta) * outer_radius_pixels);
            pix.bresenham(origin.x, origin.y, endpoint.x, endpoint.y, spoke_color, 0.2f, spoke_thickness);
        }

        draw_ring(0.5 * outer_radius_pixels, ring_color, 0.45f);
        draw_ring(outer_radius_pixels, ring_color, 0.35f);

        pix.fill_circle(origin.x, origin.y, get_geom_mean_size() / 170.0, OPAQUE_WHITE, 0.7);
    }

    void draw_ring(const double radius_pixels, const int color, const float opacity) {
        const vec2 origin = frame_center();
        const double thickness = std::max(1.0, get_geom_mean_size() / 420.0);
        pix.fill_ring(origin.x, origin.y, radius_pixels + thickness, std::max(0.0, radius_pixels - thickness), color, opacity);
    }

    void draw_reference_curve() {
        draw_curve_up_to(kFullTurn, argb(255, 100, 120, 150), 0.22f, false);
    }

    void draw_active_curve() {
        const double theta = std::clamp(static_cast<double>(state["theta"]), 0.0, kFullTurn);
        draw_curve_up_to(theta, 0, 1.0f, true);
    }

    void draw_curve_up_to(const double theta_limit, const int fallback_color, const float opacity, const bool color_by_frequency) {
        if (theta_limit <= 0.0) return;

        const int thickness = std::max(1, static_cast<int>(get_geom_mean_size() / 260.0));
        vec2 previous = raw_polar_curve_point(0.0);

        for (int sample = 1; sample <= kCurveSamples; ++sample) {
            const double progress = static_cast<double>(sample) / kCurveSamples;
            const double theta = std::min(theta_limit, progress * theta_limit);
            const vec2 current = raw_polar_curve_point(theta);

            const vec2 previous_pixel = point_to_pixel(previous);
            const vec2 current_pixel = point_to_pixel(current);

            int color = fallback_color;
            if (color_by_frequency) {
                const double midpoint_theta = 0.5 * (theta + theta_limit * (static_cast<double>(sample - 1) / kCurveSamples));
                color = color_from_frequency(frequency_from_midi_note(midi_note_from_radius(radius_from_theta(midpoint_theta))));
            }

            pix.bresenham(previous_pixel.x, previous_pixel.y, current_pixel.x, current_pixel.y, color, opacity, thickness);
            previous = current;

            if (theta >= theta_limit) break;
        }
    }

    void draw_current_radius() {
        const double theta = std::clamp(static_cast<double>(state["theta"]), 0.0, kFullTurn);
        const double radius = radius_from_theta(theta);
        const double frequency_hz = frequency_from_midi_note(midi_note_from_radius(radius));
        const int color = color_from_frequency(frequency_hz);

        const vec2 origin = frame_center();
        const vec2 point = raw_polar_curve_point(theta);
        const vec2 point_pixel = point_to_pixel(point);

        const int line_thickness = std::max(1, static_cast<int>(get_geom_mean_size() / 220.0));
        pix.bresenham(origin.x, origin.y, point_pixel.x, point_pixel.y, color, 0.95f, line_thickness);

        const double voice_strength = std::clamp(static_cast<double>(state["voice"]), 0.0, 1.0);
        const double base_radius = get_geom_mean_size() / 60.0;
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius * (1.4 + voice_strength), color, 0.25 + 0.35 * voice_strength);
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius, color, 1.0);
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius * 0.35, OPAQUE_WHITE, 1.0);
    }
};

} // namespace

void render_video() {
    PolarCurveScene scene;

    if (rendering_on()) {
        std::vector<sample_t> left;
        std::vector<sample_t> right;
        generate_surge_audio(left, right);
        stage_macroblock(GeneratedBlock(left, right), kMicroblocks);
    } else {
        stage_macroblock(SilenceBlock(kDurationSeconds), kMicroblocks);
    }

    for (int microblock = 0; microblock < kMicroblocks; ++microblock) {
        const double theta_target = kFullTurn * static_cast<double>(microblock + 1) / kMicroblocks;
        scene.manager.transition(MICRO, "theta", std::to_string(theta_target), false);
        scene.manager.transition(MICRO, "voice", std::to_string(normalized_radius_from_theta(theta_target)), false);
        scene.render_microblock();
    }
}
