#ifndef USE_GPU

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../DataObjects/ConwayGrid.h"
#include "../Host_Device_Shared/ManifoldData.c"
#include "../Host_Device_Shared/PendulumHelpers.h"
#include "../Host_Device_Shared/ThreeDimensionStructs.h"
#include "../Host_Device_Shared/helpers.h"
#include "Pixels.h"

namespace {

void warn_no_gpu_once(const char* function_name) {
    static std::unordered_set<std::string> warned;
    if (warned.insert(function_name).second) {
        std::cerr << "Warning: " << function_name
                  << " requested without CUDA/HIP support; using a CPU fallback or placeholder."
                  << std::endl;
    }
}

inline bool in_bounds(const int x, const int y, const int w, const int h) {
    return x >= 0 && x < w && y >= 0 && y < h;
}

inline uint32_t bilinear_sample(const uint32_t* pixels, const int w, const int h, const float x, const float y) {
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, h - 1);
    const int x1 = std::clamp(x0 + 1, 0, w - 1);
    const int y1 = std::clamp(y0 + 1, 0, h - 1);

    const float sx = x - x0;
    const float sy = y - y0;

    const uint32_t p00 = pixels[y0 * w + x0];
    const uint32_t p10 = pixels[y0 * w + x1];
    const uint32_t p01 = pixels[y1 * w + x0];
    const uint32_t p11 = pixels[y1 * w + x1];

    const float a0 = geta(p00) * (1.0f - sx) + geta(p10) * sx;
    const float r0 = getr(p00) * (1.0f - sx) + getr(p10) * sx;
    const float g0 = getg(p00) * (1.0f - sx) + getg(p10) * sx;
    const float b0 = getb(p00) * (1.0f - sx) + getb(p10) * sx;

    const float a1 = geta(p01) * (1.0f - sx) + geta(p11) * sx;
    const float r1 = getr(p01) * (1.0f - sx) + getr(p11) * sx;
    const float g1 = getg(p01) * (1.0f - sx) + getg(p11) * sx;
    const float b1 = getb(p01) * (1.0f - sx) + getb(p11) * sx;

    return argb(
        std::clamp(static_cast<int>(std::round(a0 * (1.0f - sy) + a1 * sy)), 0, 255),
        std::clamp(static_cast<int>(std::round(r0 * (1.0f - sy) + r1 * sy)), 0, 255),
        std::clamp(static_cast<int>(std::round(g0 * (1.0f - sy) + g1 * sy)), 0, 255),
        std::clamp(static_cast<int>(std::round(b0 * (1.0f - sy) + b1 * sy)), 0, 255)
    );
}

void clear_pixels(uint32_t* pixels, const int w, const int h, const uint32_t color = OPAQUE_BLACK) {
    std::fill(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h), color);
}

} // namespace

extern "C" int cuda_bicubic_scale(const unsigned int* input_pixels, int input_w, int input_h, unsigned int* output_pixels, int output_w, int output_h) {
    warn_no_gpu_once("cuda_bicubic_scale");

    const float x_ratio = static_cast<float>(input_w) / output_w;
    const float y_ratio = static_cast<float>(input_h) / output_h;
    for (int y = 0; y < output_h; ++y) {
        for (int x = 0; x < output_w; ++x) {
            const float src_x = x * x_ratio;
            const float src_y = y * y_ratio;
            output_pixels[y * output_w + x] = bilinear_sample(input_pixels, input_w, input_h, src_x, src_y);
        }
    }
    return 0;
}

extern "C" void cuda_overlay(
    unsigned int* background, const int bw, const int bh,
    unsigned int* foreground, const int fw, const int fh,
    const int dx, const int dy,
    const float opacity)
{
    for (int y = 0; y < fh; ++y) {
        for (int x = 0; x < fw; ++x) {
            const int bx = x + dx;
            const int by = y + dy;
            if (!in_bounds(bx, by, bw, bh)) continue;
            const int bg_index = by * bw + bx;
            const int fg_index = y * fw + x;
            background[bg_index] = color_combine(background[bg_index], foreground[fg_index], opacity);
        }
    }
}

extern "C" void cuda_overlay_with_rotation(
    unsigned int* background, const int bw, const int bh,
    unsigned int* foreground, const int fw, const int fh,
    const int dx, const int dy,
    const float opacity, const float angle_rad)
{
    const float cx = (fw - 1) * 0.5f;
    const float cy = (fh - 1) * 0.5f;
    const float cos_a = std::cos(angle_rad);
    const float sin_a = std::sin(angle_rad);

    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const float lx = static_cast<float>(bx - dx);
            const float ly = static_cast<float>(by - dy);
            const float fx = lx - cx;
            const float fy = ly - cy;

            const float src_x = cos_a * fx + sin_a * fy + cx;
            const float src_y = -sin_a * fx + cos_a * fy + cy;
            if (src_x < 0.0f || src_x >= fw - 1.0f || src_y < 0.0f || src_y >= fh - 1.0f) continue;

            const uint32_t sample = bilinear_sample(foreground, fw, fh, src_x, src_y);
            background[by * bw + bx] = color_combine(background[by * bw + bx], sample, opacity);
        }
    }
}

extern "C" void convolve_map_cuda(const unsigned int* a, const int aw, const int ah, const unsigned int* b, const int bw, const int bh, unsigned int* map, const int mapw, const int maph) {
    warn_no_gpu_once("convolve_map_cuda");

    for (int y = 0; y < maph; ++y) {
        for (int x = 0; x < mapw; ++x) {
            unsigned int sum = 0;
            const int shift_x = -bw + x + 1;
            const int shift_y = -bh + y + 1;
            const int x_min = std::max(0, shift_x);
            const int x_max = std::min(aw, shift_x + bw);
            const int y_min = std::max(0, shift_y);
            const int y_max = std::min(ah, shift_y + bh);
            for (int dx = x_min; dx < x_max; ++dx) {
                for (int dy = y_min; dy < y_max; ++dy) {
                    const unsigned int a_alpha = a[dy * aw + dx] >> 24;
                    const unsigned int b_alpha = b[(dy - shift_y) * bw + (dx - shift_x)] >> 24;
                    sum += (a_alpha > 0 && b_alpha > 0) ? 1U : 0U;
                }
            }
            map[x + y * mapw] = sum;
        }
    }
}

extern "C" void simulatePendulum(PendulumState* states, int n, int multiplier, pendulum_type dt) {
    warn_no_gpu_once("simulatePendulum");
    for (int idx = 0; idx < n; ++idx) {
        for (int i = 0; i < multiplier; ++i) {
            states[idx] = rk4Step(states[idx], dt);
        }
    }
}

extern "C" void simulate_pendulum_pair(PendulumState* states, PendulumState* pairs, pendulum_type* diffs, int n, int multiplier, pendulum_type dt) {
    warn_no_gpu_once("simulate_pendulum_pair");
    for (int idx = 0; idx < n; ++idx) {
        for (int i = 0; i < multiplier; ++i) {
            states[idx] = rk4Step(states[idx], dt);
            pairs[idx] = rk4Step(pairs[idx], dt);
            const pendulum_type p1_dist = states[idx].p1 - pairs[idx].p1;
            const pendulum_type p2_dist = states[idx].p2 - pairs[idx].p2;
            const pendulum_type theta1_dist = states[idx].theta1 - pairs[idx].theta1;
            const pendulum_type theta2_dist = states[idx].theta2 - pairs[idx].theta2;
            const pendulum_type distance = std::min<pendulum_type>(std::sqrt(
                p1_dist * p1_dist + p2_dist * p2_dist + theta1_dist * theta1_dist + theta2_dist * theta2_dist
            ), 1.0);
            diffs[idx] += distance;
        }
    }
}

extern "C" void allocate_conway_grid(Bitboard** d_board, Bitboard** d_board_2, int w_bitboards, int h_bitboards) {
    warn_no_gpu_once("allocate_conway_grid");
    const size_t count = static_cast<size_t>(w_bitboards) * static_cast<size_t>(h_bitboards);
    *d_board = new Bitboard[count]();
    *d_board_2 = new Bitboard[count]();
}

extern "C" void free_conway_grid(Bitboard* d_board, Bitboard* d_board_2) {
    delete[] d_board;
    delete[] d_board_2;
}

extern "C" void iterate_conway(Bitboard* d_board, Bitboard* d_board_2, int w_bitboards, int h_bitboards) {
    warn_no_gpu_once("iterate_conway");
    const size_t count = static_cast<size_t>(w_bitboards) * static_cast<size_t>(h_bitboards);
    std::memcpy(d_board_2, d_board, count * sizeof(Bitboard));
}

extern "C" void draw_conway(
    Bitboard*,
    Bitboard*,
    int,
    int,
    unsigned int* h_pixels, int pixels_w, int pixels_h,
    vec2, vec2, float)
{
    warn_no_gpu_once("draw_conway");
    clear_pixels(h_pixels, pixels_w, pixels_h);
}

extern "C" void render_points_on_gpu(
    unsigned int*, int, int, float, float, float,
    Point*, int,
    quat, vec3, float)
{
    warn_no_gpu_once("render_points_on_gpu");
}

extern "C" void render_lines_on_gpu(
    unsigned int*, int, int, float, int, float,
    Line*, int,
    quat, vec3, float)
{
    warn_no_gpu_once("render_lines_on_gpu");
}

extern "C" void cuda_render_surface(
    std::vector<unsigned int>&,
    int,
    int,
    int,
    int,
    int,
    unsigned int*,
    int,
    int,
    float,
    vec3,
    quat,
    const vec3&,
    const vec3&,
    const vec3&,
    const vec3&,
    const float,
    const float,
    float,
    float,
    float)
{
    warn_no_gpu_once("cuda_render_surface");
}

extern "C" void beaver_grid_cuda(int, int, unsigned int* pixels, int w, int h, vec2, vec2, int) {
    warn_no_gpu_once("beaver_grid_cuda");
    clear_pixels(pixels, w, h);
}

extern "C" void beaver_grid_TNF_cuda(unsigned int* pixels, int w, int h, vec2, vec2, int) {
    warn_no_gpu_once("beaver_grid_TNF_cuda");
    clear_pixels(pixels, w, h);
}

extern "C" void beaver_grid_TNF_3D_cuda(unsigned int* pixels, int w, int h, vec3, quat, float, vec3, vec3, float, float, float, vec3, int) {
    warn_no_gpu_once("beaver_grid_TNF_3D_cuda");
    clear_pixels(pixels, w, h);
}

extern "C" void color_complex_polynomial(
    unsigned int* h_pixels,
    int w,
    int h,
    const float*,
    const float*,
    int,
    float, float,
    float, float,
    float,
    float)
{
    warn_no_gpu_once("color_complex_polynomial");
    clear_pixels(h_pixels, w, h);
}

extern "C" void mandelbrot_render(
    const int width, const int height,
    const vec2,
    const vec2,
    const std::complex<float>, const std::complex<float>, const std::complex<float>,
    const vec3,
    int,
    float,
    float,
    unsigned int internal_color,
    unsigned int* depths)
{
    warn_no_gpu_once("mandelbrot_render");
    clear_pixels(depths, width, height, internal_color);
}

extern "C" void draw_root_fractal(unsigned int* pixels, int w, int h, std::complex<float>, std::complex<float>, float, float, float, float, float, float, float, float) {
    warn_no_gpu_once("draw_root_fractal");
    clear_pixels(pixels, w, h);
}

extern "C" void render_raymarch(
    const int width, const int height,
    const vec3&, const quat&, float,
    const vec3&,
    const int, const int,
    unsigned int* colors)
{
    warn_no_gpu_once("render_raymarch");
    clear_pixels(colors, width, height);
}

extern "C" void compute_repulsion_cuda(vec4*, vec4*, const int*, const int*, const int*, int, int, float, float, float, const float, const float, const int) {
    warn_no_gpu_once("compute_repulsion_cuda");
}

extern "C" void cuda_render_manifold(
    uint32_t* pixels, const int w, const int h,
    const ManifoldData*, const int,
    const vec3, const quat,
    const float, const float,
    const float, const float,
    const uint32_t*, const int, const int)
{
    warn_no_gpu_once("cuda_render_manifold");
    clear_pixels(pixels, w, h);
}

extern "C" void cuda_render_geodesics_2d(
    uint32_t* pixels, const int w, const int h,
    const ManifoldData&,
    const vec2, const vec2,
    const int, const int, const float,
    const vec3, const quat,
    const float, const float, const float)
{
    warn_no_gpu_once("cuda_render_geodesics_2d");
    clear_pixels(pixels, w, h, TRANSPARENT_BLACK);
}

extern "C" void launch_cuda_surface_raymarch(
    uint32_t* h_pixels, int w, int h,
    int, ResolvedStateEquationComponent*,
    int, ResolvedStateEquationComponent*,
    int, ResolvedStateEquationComponent*,
    int, ResolvedStateEquationComponent*,
    int,
    const quat&, const vec3&,
    float, float, float, const vec3&)
{
    warn_no_gpu_once("launch_cuda_surface_raymarch");
    clear_pixels(h_pixels, w, h);
}

extern "C" uint32_t* cuda_copy_texture_to_device(const uint32_t* h_tex_pixels, const int tex_w, const int tex_h) {
    warn_no_gpu_once("cuda_copy_texture_to_device");
    const size_t count = static_cast<size_t>(tex_w) * static_cast<size_t>(tex_h);
    auto* data = new uint32_t[count];
    std::copy(h_tex_pixels, h_tex_pixels + count, data);
    return data;
}

extern "C" void cuda_free_texture(uint32_t* d_tex_pixels) {
    delete[] d_tex_pixels;
}

#endif
