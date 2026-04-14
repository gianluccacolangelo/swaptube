#include "VisualMedia.h"

#include <png.h>
#include <vector>
#include <stdexcept>
#include <librsvg/rsvg.h>
#include <sys/stat.h>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <limits.h>
#include <unistd.h>
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fstream>
#include "../Core/Smoketest.h"

using namespace std;

namespace {

bool is_executable_file(const string& path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

string find_executable_in_path(const string& name) {
    const char* path_env = getenv("PATH");
    if (path_env == nullptr) {
        return "";
    }

    string path_list(path_env);
    stringstream ss(path_list);
    string directory;
    while (getline(ss, directory, ':')) {
        if (directory.empty()) {
            continue;
        }
        const string candidate = directory + "/" + name;
        if (is_executable_file(candidate)) {
            return candidate;
        }
    }
    return "";
}

string expand_home_path(const string& path) {
    if (path.rfind("~/", 0) != 0) {
        return path;
    }

    const char* home = getenv("HOME");
    if (home == nullptr) {
        return path;
    }
    return string(home) + path.substr(1);
}

string get_env_string(const char* name) {
    const char* value = getenv(name);
    return value == nullptr ? "" : string(value);
}

string basename_of_path(const string& path) {
    const size_t separator = path.find_last_of('/');
    if (separator == string::npos) {
        return path;
    }
    return path.substr(separator + 1);
}

string canonical_or_original_path(const string& path) {
    char resolved_path[PATH_MAX];
    if (realpath(path.c_str(), resolved_path) != nullptr) {
        return string(resolved_path);
    }
    return path;
}

bool is_probably_microtex_binary(const string& path) {
    if (!is_executable_file(path)) {
        return false;
    }

    const string binary_name = basename_of_path(canonical_or_original_path(path));
    return binary_name != "pdftex"
        && binary_name != "latex"
        && binary_name != "pdflatex"
        && binary_name != "xelatex"
        && binary_name != "lualatex";
}

string shell_quote(const string& value) {
    string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

void ensure_directory_exists(const string& path) {
    struct stat directory_stat;
    if (stat(path.c_str(), &directory_stat) == 0) {
        if (S_ISDIR(directory_stat.st_mode)) {
            return;
        }
        throw runtime_error("Path exists but is not a directory: " + path);
    }

    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        throw runtime_error("Failed to create directory " + path + ": " + string(strerror(errno)));
    }
}

string find_microtex_latex_binary() {
    const string env_bin = get_env_string("MICROTEX_LATEX_BIN");
    if (!env_bin.empty()) {
        const string configured_path = expand_home_path(env_bin);
        if (is_probably_microtex_binary(configured_path)) {
            return configured_path;
        }
    }

    const string env_dir = get_env_string("MICROTEX_DIR");
    if (!env_dir.empty()) {
        const string configured_path = expand_home_path(env_dir + "/build/LaTeX");
        if (is_probably_microtex_binary(configured_path)) {
            return configured_path;
        }
    }

    for (const string& candidate : {
        "../../MicroTeX-master/build/LaTeX",
        "../MicroTeX-master/build/LaTeX",
        "../../MicroTeX/build/LaTeX",
        "../MicroTeX/build/LaTeX"
    }) {
        if (is_probably_microtex_binary(candidate)) {
            return candidate;
        }
    }

    const string path_candidate = find_executable_in_path("LaTeX");
    return is_probably_microtex_binary(path_candidate) ? path_candidate : "";
}

string find_texlive_xelatex_binary() {
    return find_executable_in_path("xelatex");
}

string find_pdftocairo_binary() {
    return find_executable_in_path("pdftocairo");
}

bool texlive_font_config_present() {
    return !get_env_string("SWAPTUBE_TEX_FONT").empty()
        || !get_env_string("SWAPTUBE_TEX_MAIN_FONT").empty()
        || !get_env_string("SWAPTUBE_TEX_SANS_FONT").empty()
        || !get_env_string("SWAPTUBE_TEX_MONO_FONT").empty()
        || !get_env_string("SWAPTUBE_TEX_MATH_FONT").empty();
}

string get_latex_backend_preference() {
    return get_env_string("SWAPTUBE_LATEX_BACKEND");
}

string sanitize_tex_font_name(const string& font_name) {
    if (font_name.find_first_of("{}%\\\n\r") != string::npos) {
        throw runtime_error("Invalid font name in SWAPTUBE_TEX_* environment variable: " + font_name);
    }
    return font_name;
}

string build_texlive_font_setup() {
    const string shared_font = get_env_string("SWAPTUBE_TEX_FONT");
    string main_font = get_env_string("SWAPTUBE_TEX_MAIN_FONT");
    string sans_font = get_env_string("SWAPTUBE_TEX_SANS_FONT");
    const string mono_font = get_env_string("SWAPTUBE_TEX_MONO_FONT");
    const string math_font = get_env_string("SWAPTUBE_TEX_MATH_FONT");

    if (main_font.empty()) {
        main_font = shared_font;
    }
    if (sans_font.empty()) {
        sans_font = shared_font;
    }

    string setup;
    setup += "\\defaultfontfeatures{Ligatures=TeX}\n";
    if (!main_font.empty()) {
        setup += "\\setmainfont{" + sanitize_tex_font_name(main_font) + "}\n";
    }
    if (!sans_font.empty()) {
        setup += "\\setsansfont{" + sanitize_tex_font_name(sans_font) + "}\n";
    }
    if (!mono_font.empty()) {
        setup += "\\setmonofont{" + sanitize_tex_font_name(mono_font) + "}\n";
    }
    if (!math_font.empty()) {
        setup += "\\setmathfont{" + sanitize_tex_font_name(math_font) + "}\n";
    }
    return setup;
}

string build_texlive_latex_document(const string& latex) {
    string document;
    document += "\\documentclass[preview,varwidth,border=0pt]{standalone}\n";
    document += "\\usepackage{amsmath}\n";
    document += "\\usepackage{amssymb}\n";
    document += "\\usepackage{xcolor}\n";
    document += "\\usepackage{fontspec}\n";
    document += "\\usepackage{unicode-math}\n";
    document += build_texlive_font_setup();
    document += "\\begin{document}\n";
    document += "\\color[HTML]{FFFFFF}\n";
    if (latex.find("\\begin{") != string::npos) {
        document += latex;
        document += "\n";
    } else {
        // Most existing swaptube strings are equation fragments rather than full documents.
        document += "\\[\n";
        document += latex;
        document += "\n\\]\n";
    }
    document += "\\end{document}\n";
    return document;
}

void render_latex_with_microtex(const string& latex, const string& output_svg_path, const string& latex_binary) {
    const string command =
        shell_quote(latex_binary) +
        " -headless -foreground=#ffffffff " +
        shell_quote("-input=" + latex) +
        " -output=" +
        shell_quote(output_svg_path) +
        " >/dev/null 2>&1";

    const int result = system(command.c_str());
    if (result != 0 || access(output_svg_path.c_str(), F_OK) == -1) {
        throw runtime_error("Failed to generate LaTeX with MicroTeX.");
    }
}

void render_latex_with_texlive(const string& latex, const string& output_svg_path, const string& xelatex_binary, const string& pdftocairo_binary) {
    const string output_stem = output_svg_path.substr(0, output_svg_path.size() - 4);
    const string tex_path = output_stem + ".tex";
    const string pdf_path = output_stem + ".pdf";
    const string log_path = output_stem + ".log";

    ofstream tex_output(tex_path);
    if (!tex_output) {
        throw runtime_error("Failed to create TeX source file " + tex_path);
    }
    tex_output << build_texlive_latex_document(latex);
    tex_output.close();

    const string xelatex_command =
        shell_quote(xelatex_binary) +
        " -interaction=nonstopmode -halt-on-error -output-directory=" +
        shell_quote("io_in/latex") +
        " " +
        shell_quote(tex_path) +
        " >/dev/null 2>&1";
    const int xelatex_result = system(xelatex_command.c_str());
    if (xelatex_result != 0 || access(pdf_path.c_str(), F_OK) == -1) {
        throw runtime_error("Failed to generate LaTeX with XeLaTeX. See " + log_path);
    }

    const string pdftocairo_command =
        shell_quote(pdftocairo_binary) +
        " -svg " +
        shell_quote(pdf_path) +
        " " +
        shell_quote(output_svg_path) +
        " >/dev/null 2>&1";
    const int pdftocairo_result = system(pdftocairo_command.c_str());
    if (pdftocairo_result != 0 || access(output_svg_path.c_str(), F_OK) == -1) {
        throw runtime_error("Failed to convert XeLaTeX output PDF to SVG.");
    }
}

Pixels make_latex_placeholder(const ScalingParams& scaling_params) {
    int width = 0;
    int height = 0;
    if (scaling_params.mode == ScalingMode::BoundingBox) {
        width = max(24, static_cast<int>(round(scaling_params.max_width * 0.85f)));
        height = max(16, static_cast<int>(round(scaling_params.max_height * 0.55f)));
    } else {
        const double scale = max(0.25, scaling_params.scale_factor);
        width = max(24, static_cast<int>(round(120.0 * scale)));
        height = max(16, static_cast<int>(round(36.0 * scale)));
    }

    Pixels placeholder(width, height);
    const int radius = max(2, min(width, height) / 6);
    const int border = max(1, min(width, height) / 14);
    placeholder.rounded_rect(0, 0, width, height, radius, 0x99ffffff);
    if (width > border * 2 && height > border * 2) {
        placeholder.rounded_rect(border, border, width - border * 2, height - border * 2, max(1, radius - border), TRANSPARENT_BLACK);
    }
    placeholder.bresenham(border, border, width - border - 1, height - border - 1, 0x99ffffff, 1.0f, border);
    placeholder.bresenham(width - border - 1, border, border, height - border - 1, 0x99ffffff, 1.0f, border);
    return placeholder;
}

Pixels latex_placeholder_with_warning(const string& reason, ScalingParams& scaling_params) {
    static bool warned_once = false;
    if (!warned_once) {
        cout << "latex_to_pix: " << reason << ". Using a placeholder during smoketest." << endl;
        warned_once = true;
    }
    scaling_params.scale_factor = 1;
    return make_latex_placeholder(scaling_params);
}

Pixels make_missing_png_placeholder() {
    Pixels placeholder(256, 256);
    const int dark = 0xff303030;
    const int light = 0xff707070;
    const int square = 32;
    for (int y = 0; y < placeholder.h; ++y) {
        for (int x = 0; x < placeholder.w; ++x) {
            const bool use_light = ((x / square) + (y / square)) % 2 == 0;
            placeholder.set_pixel_carelessly(x, y, use_light ? light : dark);
        }
    }
    placeholder.bresenham(16, 16, placeholder.w - 17, placeholder.h - 17, OPAQUE_WHITE, 1.0f, 6);
    placeholder.bresenham(placeholder.w - 17, 16, 16, placeholder.h - 17, OPAQUE_WHITE, 1.0f, 6);
    return placeholder;
}

Pixels png_placeholder_with_warning(const string& fullpath) {
    static unordered_set<string> warned_paths;
    if (warned_paths.insert(fullpath).second) {
        cout << "png_to_pix: missing input PNG " << fullpath << ". Using a placeholder during smoketest." << endl;
    }
    return make_missing_png_placeholder();
}

} // namespace

void pix_to_png(const Pixels& pix, const string& filename) {
    if(pix.w * pix.h == 0) return; // cowardly exit.

    // Open the file for writing (binary mode)
    FILE* fp = fopen(("io_out/" + filename + ".png").c_str(), "wb");
    if (!fp) {
        throw runtime_error("Failed to open png file for writing: " + filename);
    }

    // Initialize write structure
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        throw runtime_error("Failed to create png write struct.");
    }

    // Initialize info structure
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        throw runtime_error("Failed to create png info struct.");
    }

    // Set up error handling (required without using the default error handlers)
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        throw runtime_error("Error during PNG creation.");
    }

    // Set up output control
    png_init_io(png, fp);

    // Write header (8 bit color depth)
    png_set_IHDR(png, info, pix.w, pix.h,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    // Allocate memory for one row
    png_bytep row = (png_bytep)malloc(4 * pix.w * sizeof(png_byte));
    if (!row) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        throw runtime_error("Failed to allocate memory for row.");
    }

    // Write image data
    for (int y = 0; y < pix.h; y++) {
        for (int x = 0; x < pix.w; x++) {
            int pixel = pix.get_pixel_carelessly(x, y);
            uint8_t a = (pixel >> 24) & 0xFF;
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;
            row[x*4 + 0] = r;
            row[x*4 + 1] = g;
            row[x*4 + 2] = b;
            row[x*4 + 3] = a;
        }
        png_write_row(png, row);
    }

    // End write
    png_write_end(png, nullptr);

    // Free allocated memory
    free(row);

    // Cleanup
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}

static gboolean get_svg_intrinsic_size(RsvgHandle *handle, gdouble* width, gdouble* height) {
    #if LIBRSVG_CHECK_VERSION(2, 52, 0)
        return rsvg_handle_get_intrinsic_size_in_pixels(handle, width, height);
    #else
        RsvgDimensionData dim;
        rsvg_handle_get_dimensions(handle, &dim);
        if (dim.width <= 0 || dim.height <= 0) return FALSE;
        *width  = dim.width;
        *height = dim.height;
        return TRUE;
    #endif
}

Pixels svg_to_pix(const string& filename_with_or_without_suffix, ScalingParams& scaling_params) {
    // Check if the filename already ends with ".svg"
    string filename = "io_in/" + filename_with_or_without_suffix;
    if (filename.length() < 4 || filename.substr(filename.length() - 4) != ".svg") {
        filename += ".svg";  // Append the ".svg" suffix if it's not present
    }

    // Load SVG
    GError* error = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(filename.c_str(), &error);
    if (!handle) {
        const string error_message = error != nullptr ? error->message : "Unknown librsvg error";
        if (error != nullptr) {
            g_error_free(error);
        }
        string error_str = "Error loading SVG file " + filename + ": " + error_message;
        throw runtime_error(error_str);
    }

    gdouble gwidth, gheight;
    if (!get_svg_intrinsic_size(handle, &gwidth, &gheight))
        throw runtime_error("Could not get intrinsic size of SVG file " + filename);

    // Calculate scale factor
    if (scaling_params.mode == ScalingMode::BoundingBox) {
        scaling_params.scale_factor = min(
            static_cast<double>(scaling_params.max_width) / gwidth,
            static_cast<double>(scaling_params.max_height) / gheight
        );
    } else if (scaling_params.scale_factor <= 0) {
        throw runtime_error("Invalid scale factor: " + to_string(scaling_params.scale_factor));
    }

    int width  = round(gwidth  * scaling_params.scale_factor);
    int height = round(gheight * scaling_params.scale_factor);

    if (width <= 0 || height <= 0) {
        g_object_unref(handle);
        throw runtime_error("Computed output size for SVG file " + filename + " is invalid: width=" + to_string(width) + ", height=" + to_string(height) + ", scaling factor=" + to_string(scaling_params.scale_factor));
    }

    Pixels ret(width, height);

    // Allocate pixel buffer
    vector<uint8_t> raw_data(width * height * 4, 0);

    // Create cairo surface and context
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        raw_data.data(), CAIRO_FORMAT_ARGB32, width, height, width * 4
    ); 
    cairo_t* cr = cairo_create(surface);

    // Set scale
    cairo_scale(cr, scaling_params.scale_factor, scaling_params.scale_factor);

    // Define viewport for rendering
    RsvgRectangle viewport = {
        .x = 0,
        .y = 0,
        .width = gwidth,
        .height = gheight
    };

    if (viewport.width <= 0 || viewport.height <= 0) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        throw runtime_error("Invalid viewport size for SVG file " + filename);
    }

    // Render SVG
    if (!rsvg_handle_render_document(handle, cr, &viewport, &error)) {
        const string error_message = error != nullptr ? error->message : "Unknown librsvg render error";
        if (error != nullptr) {
            g_error_free(error);
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        throw runtime_error("Failed to render SVG file " + filename + ": " + error_message);
    }

    // Copy pixels into Pixels object
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int offset = (y * width + x) * 4;
            ret.set_pixel_carelessly(x, y, argb(
                raw_data[offset + 3],  // Alpha
                raw_data[offset + 2],  // Red
                raw_data[offset + 1],  // Green
                raw_data[offset]       // Blue
            ));
        }
    }

    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(handle);

    return crop_by_alpha(ret);
}

void png_to_pix(Pixels& pix, const string& filename_with_or_without_suffix) {
    // Check if the filename already ends with ".png"
    string filename = filename_with_or_without_suffix;
    if (filename.length() < 4 || filename.substr(filename.length() - 4) != ".png") {
        filename += ".png";  // Append the ".png" suffix if it's not present
    }

    string fullpath = "io_in/" + filename;

    // Check cache
    static unordered_map<string, Pixels> png_cache;
    auto it = png_cache.find(fullpath);
    if (it != png_cache.end()) {
        pix = it->second;
        return;
    }

    // Open the PNG file
    FILE* fp = fopen(fullpath.c_str(), "rb");
    if (!fp) {
        if (!rendering_on()) {
            pix = png_placeholder_with_warning(fullpath);
            png_cache[fullpath] = pix;
            return;
        }
        throw runtime_error("Failed to open PNG file " + fullpath);
    }

    // Create and initialize the png_struct
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        throw runtime_error("Failed to create png read struct.");
    }

    // Create and initialize the png_info
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        throw runtime_error("Failed to create png info struct.");
    }

    // Set up error handling (required without using the default error handlers)
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        throw runtime_error("Error during PNG creation.");
    }

    // Initialize input/output for libpng
    png_init_io(png, fp);
    png_read_info(png, info);

    // Get image info
    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    // Read any color_type into 8bit depth, RGBA format.
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    // Read image data
    vector<png_bytep> row_pointers(height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }
    png_read_image(png, row_pointers.data());

    // Create a Pixels object
    pix = Pixels(width, height);

    // Copy data to Pixels object
    for (int y = 0; y < height; y++) {
        png_bytep row = row_pointers[y];
        for (int x = 0; x < width; x++) {
            png_bytep px = &(row[x * 4]);
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b = px[2];
            uint8_t a = px[3];
            pix.set_pixel_carelessly(x, y, argb(a, r, g, b));
        }
    }

    // Free memory and close file
    for (int y = 0; y < height; y++) {
        free(row_pointers[y]);
    }
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    // Store in cache
    png_cache[fullpath] = pix;
}

// Custom hash and equality for pair<string, pair<int,int>>
struct StringIntPairHash {
    size_t operator()(const pair<string, pair<int, int>>& p) const noexcept {
        size_t h1 = std::hash<string>{}(p.first);
        size_t h2 = (static_cast<size_t>(p.second.first) << 32) ^ static_cast<size_t>(p.second.second);
        // boost-like mix
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1<<6) + (h1>>2));
    }
};
struct StringIntPairEq {
    bool operator()(const pair<string, pair<int, int>>& a, const pair<string, pair<int, int>>& b) const noexcept {
        return a.first == b.first && a.second.first == b.second.first && a.second.second == b.second.second;
    }
};

void png_to_pix_bounding_box(Pixels& pix, const string& filename, int w, int h) {
    static unordered_map<pair<string, pair<int, int>>, Pixels, StringIntPairHash, StringIntPairEq> png_bounding_box_cache;
    auto key = make_pair(filename, make_pair(w, h));
    auto it = png_bounding_box_cache.find(key);
    if (it != png_bounding_box_cache.end()) {
        pix = it->second;
        return;
    }

    Pixels image;
    png_to_pix(image, filename);

    image.scale_to_bounding_box(w, h, pix);

    // Store in cache with scale
    png_bounding_box_cache[key] = pix;
}

// Create an unordered_map to store the cached results
unordered_map<string, pair<Pixels, double>> latex_cache;

static string generate_cache_key(const string& text, const ScalingParams& scaling_params, const string& render_signature) {
    hash<string> hasher;
    string key = text + "_" + render_signature + "_" +
                 to_string(static_cast<int>(scaling_params.mode)) + "_" +
                 to_string(scaling_params.max_width) + "_" +
                 to_string(scaling_params.max_height) + "_" +
                 to_string(scaling_params.scale_factor);
    return to_string(hasher(key));
}

/*
 * We prefer MicroTeX for legacy compatibility, but can fall back to XeLaTeX
 * plus pdftocairo when only a regular TeX Live install is present.
 */
Pixels latex_to_pix(const string& latex, ScalingParams& scaling_params) {
    const string backend_preference = get_latex_backend_preference();
    const string microtex_binary = find_microtex_latex_binary();
    const string xelatex_binary = find_texlive_xelatex_binary();
    const string pdftocairo_binary = find_pdftocairo_binary();

    string backend;
    if (backend_preference == "microtex") {
        backend = "microtex";
    } else if (backend_preference == "texlive") {
        backend = "texlive";
    } else if (!backend_preference.empty()) {
        throw runtime_error("Unknown SWAPTUBE_LATEX_BACKEND value: " + backend_preference + ". Use 'microtex' or 'texlive'.");
    } else if (texlive_font_config_present()) {
        backend = "texlive";
    } else if (!microtex_binary.empty()) {
        backend = "microtex";
    } else if (!xelatex_binary.empty() && !pdftocairo_binary.empty()) {
        backend = "texlive";
    }

    const string render_signature =
        "backend=" + backend +
        ";preference=" + backend_preference +
        ";font=" + get_env_string("SWAPTUBE_TEX_FONT") +
        ";main=" + get_env_string("SWAPTUBE_TEX_MAIN_FONT") +
        ";sans=" + get_env_string("SWAPTUBE_TEX_SANS_FONT") +
        ";mono=" + get_env_string("SWAPTUBE_TEX_MONO_FONT") +
        ";math=" + get_env_string("SWAPTUBE_TEX_MATH_FONT");

    // Generate a cache key based on the equation, backend, and scaling parameters.
    string cache_key = generate_cache_key(latex, scaling_params, render_signature);

    // Check if the result is already in the cache.
    auto it = latex_cache.find(cache_key);
    if (it != latex_cache.end()) {
        scaling_params.scale_factor = it->second.second;
        return it->second.first;
    }

    cout << "Generating LaTeX for: " << latex << endl;

    hash<string> hasher;
    const string latex_dir = "io_in/latex";
    ensure_directory_exists(latex_dir);
    const string name_without_folder = to_string(hasher(render_signature + "\n" + latex)) + ".svg";
    const string name = latex_dir + "/" + name_without_folder;

    if (access(name.c_str(), F_OK) == -1) {
        if (backend == "microtex") {
            if (microtex_binary.empty()) {
                if (!rendering_on()) {
                    Pixels placeholder = latex_placeholder_with_warning("MicroTeX LaTeX binary was not found", scaling_params);
                    latex_cache[cache_key] = make_pair(placeholder, scaling_params.scale_factor);
                    return placeholder;
                }
                throw runtime_error("MicroTeX LaTeX binary not found. Set MICROTEX_LATEX_BIN or MICROTEX_DIR, or set SWAPTUBE_LATEX_BACKEND=texlive to use XeLaTeX.");
            }

            try {
                render_latex_with_microtex(latex, name, microtex_binary);
            } catch (const runtime_error& error) {
                if (!rendering_on()) {
                    Pixels placeholder = latex_placeholder_with_warning(error.what(), scaling_params);
                    latex_cache[cache_key] = make_pair(placeholder, scaling_params.scale_factor);
                    return placeholder;
                }
                throw;
            }
        } else if (backend == "texlive") {
            if (xelatex_binary.empty() || pdftocairo_binary.empty()) {
                const string missing_piece = xelatex_binary.empty() ? "XeLaTeX" : "pdftocairo";
                if (!rendering_on()) {
                    Pixels placeholder = latex_placeholder_with_warning(missing_piece + " was not found", scaling_params);
                    latex_cache[cache_key] = make_pair(placeholder, scaling_params.scale_factor);
                    return placeholder;
                }
                throw runtime_error(missing_piece + " was not found. Install TeX Live with xelatex and poppler's pdftocairo.");
            }

            try {
                render_latex_with_texlive(latex, name, xelatex_binary, pdftocairo_binary);
            } catch (const runtime_error& error) {
                if (!rendering_on()) {
                    Pixels placeholder = latex_placeholder_with_warning(error.what(), scaling_params);
                    latex_cache[cache_key] = make_pair(placeholder, scaling_params.scale_factor);
                    return placeholder;
                }
                throw;
            }
        } else {
            if (!rendering_on()) {
                Pixels placeholder = latex_placeholder_with_warning("No LaTeX renderer was found", scaling_params);
                latex_cache[cache_key] = make_pair(placeholder, scaling_params.scale_factor);
                return placeholder;
            }
            throw runtime_error("No LaTeX renderer was found. Install MicroTeX or use TeX Live with xelatex and pdftocairo.");
        }
    }

    Pixels pixels = svg_to_pix("latex/" + name_without_folder, scaling_params);
    latex_cache[cache_key] = make_pair(pixels, scaling_params.scale_factor);
    return pixels;
}

void pdf_page_to_pix(Pixels& pix, const string& pdf_filename_without_suffix, const int page_number) {
    if (page_number < 1) {
        throw runtime_error("PDF page number is 1-indexed and should be positive.");
    }
    if (page_number >= 100) {
        throw runtime_error("PDF page number too large; pdf_page_to_pix only supports up to 99 pages. (TODO)");
    }

    // HOW TO MAKE PAGES:
    // pdftocairo -png -f 1 -l 3 -r 300 paper.pdf prefix
    // (This makes 3 pages at 300 DPI, named prefix-01.png, prefix-02.png, prefix-03.png)
    const string resolved_filename_without_suffix = "io_in/" + pdf_filename_without_suffix;
    if (resolved_filename_without_suffix.length() >= 4 && resolved_filename_without_suffix.substr(resolved_filename_without_suffix.length() - 4) == ".pdf") {
        throw runtime_error("pdf_page_to_pix: please provide the pdf filename without the .pdf suffix.");
    }
    const string resolved_filename_with_suffix = resolved_filename_without_suffix + ".pdf";

    const string png_filename = resolved_filename_without_suffix + "-" + (page_number < 10 ? "0" : "") + to_string(page_number) + ".png";

    struct stat buffer;
    bool png_file_exists = false;
    if (stat(png_filename.c_str(), &buffer) == 0) {
        png_file_exists = true;
    }

    // Execute pdftocairo command to convert the specified page to PNG
    if (!png_file_exists) {
        cout << "Converting PDF page " << page_number << " to PNG..." << endl;
        const string page_number_str = to_string(page_number);
        const string command = "pdftocairo -png -f " + page_number_str + " -l " + page_number_str + " -r 300 " + resolved_filename_with_suffix + " " + resolved_filename_without_suffix;
        int result = system(command.c_str());
        if (result != 0) {
            throw runtime_error("Failed to convert PDF page to PNG using pdftocairo.");
        }
    }

    // Verify that the PNG file was created
    if (stat(png_filename.c_str(), &buffer) != 0) {
        throw runtime_error("PNG file was not created after pdftocairo command.");
    }

    // Load the generated PNG into the provided Pixels object
    const string png_filename_without_prefix = png_filename.substr(6);
    png_to_pix(pix, png_filename_without_prefix);
}
