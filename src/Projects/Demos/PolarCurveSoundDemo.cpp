#include "../Scenes/Common/CoordinateScene.h"
#include "../IO/AudioWriter.h"
#include "../IO/Writer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFullTurn = 2.0 * kPi;
constexpr double kDurationSeconds = 8.0;
constexpr int kMicroblocks = 16;
constexpr int kCurveSamples = 2048;

double radius_from_theta(const double theta) {
    return std::cos(theta);
}

vec2 polar_curve_point(const double theta) {
    const double radius = radius_from_theta(theta);
    return vec2(radius * std::cos(theta), radius * std::sin(theta));
}

double frequency_from_radius(const double radius) {
    // Maps r in [-1, 1] onto the requested frequency band [200, 1000].
    return 600.0 + 400.0 * radius;
}

int color_from_frequency(const double frequency_hz) {
    const double normalized = std::clamp((frequency_hz - 200.0) / 800.0, 0.0, 1.0);
    return HSVtoRGB(0.62 - normalized * 0.54, 0.85, 1.0);
}

void generate_audio(std::vector<sample_t>& left, std::vector<sample_t>& right) {
    const int samplerate = get_audio_samplerate_hz();
    const int total_samples = static_cast<int>(kDurationSeconds * samplerate);
    const double attack_release_seconds = 0.02;
    const int attack_release_samples = std::max(1, static_cast<int>(attack_release_seconds * samplerate));

    left.reserve(total_samples);
    right.reserve(total_samples);

    double phase = 0.0;
    for (int i = 0; i < total_samples; ++i) {
        const double progress = total_samples > 1 ? static_cast<double>(i) / (total_samples - 1) : 0.0;
        const double theta = progress * kFullTurn;
        const double radius = radius_from_theta(theta);
        const double frequency_hz = frequency_from_radius(radius);

        phase += kFullTurn * frequency_hz / samplerate;

        const int samples_from_edge = std::min(i, total_samples - 1 - i);
        const double fade = std::min(1.0, static_cast<double>(samples_from_edge) / attack_release_samples);
        const double amplitude = 0.15 * fade;
        const sample_t sample = float_to_sample(amplitude * std::sin(phase));

        left.push_back(sample);
        right.push_back(sample);
    }
}

class PolarCurveScene : public CoordinateScene {
public:
    PolarCurveScene(const vec2& dimensions = vec2(1, 1))
        : CoordinateScene(dimensions) {
        manager.set({
            {"theta", "0"},
            {"center_x", "0.1"},
            {"center_y", "0"},
            {"zoom", "2.5"},
            {"ticks_opacity", "0"},
            {"construction_opacity", "0"},
            {"zero_crosshair_opacity", "0"},
        });
    }

    void draw() override {
        draw_polar_grid();
        draw_reference_curve();
        draw_active_curve();
        draw_current_radius();
    }

    const StateQuery populate_state_query() const override {
        StateQuery query = CoordinateScene::populate_state_query();
        state_query_insert_multiple(query, {"theta", "voice"});
        return query;
    }

private:
    void draw_polar_grid() {
        const vec2 origin = point_to_pixel(vec2(0, 0));
        const int spoke_color = argb(255, 70, 88, 112);
        const int ring_color = argb(255, 55, 70, 94);
        const int spoke_thickness = std::max(1, static_cast<int>(get_geom_mean_size() / 360.0));
        const double outer_radius = 1.2;

        for (int i = 0; i < 8; ++i) {
            const double theta = kFullTurn * i / 8.0;
            const vec2 endpoint = point_to_pixel(vec2(std::cos(theta) * outer_radius, std::sin(theta) * outer_radius));
            pix.bresenham(origin.x, origin.y, endpoint.x, endpoint.y, spoke_color, 0.2f, spoke_thickness);
        }

        draw_ring(0.5, ring_color, 0.45f);
        draw_ring(1.0, ring_color, 0.35f);

        pix.fill_circle(origin.x, origin.y, get_geom_mean_size() / 170.0, OPAQUE_WHITE, 0.7);
    }

    void draw_ring(const double radius, const int color, const float opacity) {
        const vec2 origin = point_to_pixel(vec2(0, 0));
        const vec2 ring_point = point_to_pixel(vec2(radius, 0));
        const double radius_pixels = std::abs(ring_point.x - origin.x);
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
        vec2 previous = polar_curve_point(0.0);

        for (int sample = 1; sample <= kCurveSamples; ++sample) {
            const double progress = static_cast<double>(sample) / kCurveSamples;
            const double theta = std::min(theta_limit, progress * theta_limit);
            const vec2 current = polar_curve_point(theta);

            const vec2 previous_pixel = point_to_pixel(previous);
            const vec2 current_pixel = point_to_pixel(current);

            int color = fallback_color;
            if (color_by_frequency) {
                const double midpoint_theta = 0.5 * (theta + theta_limit * (static_cast<double>(sample - 1) / kCurveSamples));
                color = color_from_frequency(frequency_from_radius(radius_from_theta(midpoint_theta)));
            }

            pix.bresenham(previous_pixel.x, previous_pixel.y, current_pixel.x, current_pixel.y, color, opacity, thickness);
            previous = current;

            if (theta >= theta_limit) break;
        }
    }

    void draw_current_radius() {
        const double theta = std::clamp(static_cast<double>(state["theta"]), 0.0, kFullTurn);
        const double radius = radius_from_theta(theta);
        const double frequency_hz = frequency_from_radius(radius);
        const int color = color_from_frequency(frequency_hz);

        const vec2 origin = point_to_pixel(vec2(0, 0));
        const vec2 point = polar_curve_point(theta);
        const vec2 point_pixel = point_to_pixel(point);

        const int line_thickness = std::max(1, static_cast<int>(get_geom_mean_size() / 220.0));
        pix.bresenham(origin.x, origin.y, point_pixel.x, point_pixel.y, color, 0.95f, line_thickness);

        const double voice_strength = std::min(1.0, std::abs(state["voice"]) * 7.0);
        const double base_radius = get_geom_mean_size() / 60.0;
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius * (1.4 + voice_strength), color, 0.25 + 0.35 * voice_strength);
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius, color, 1.0);
        pix.fill_circle(point_pixel.x, point_pixel.y, base_radius * 0.35, OPAQUE_WHITE, 1.0);
    }
};

} // namespace

void render_video() {
    PolarCurveScene scene;

    std::vector<sample_t> left;
    std::vector<sample_t> right;
    generate_audio(left, right);

    stage_macroblock(GeneratedBlock(left, right), kMicroblocks);
    for (int microblock = 0; microblock < kMicroblocks; ++microblock) {
        const double theta_target = kFullTurn * static_cast<double>(microblock + 1) / kMicroblocks;
        // Use linear interpolation so the visual theta progression matches the audio sweep.
        scene.manager.transition(MICRO, "theta", std::to_string(theta_target), false);
        scene.render_microblock();
    }
}
