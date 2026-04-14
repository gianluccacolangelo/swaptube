#include "../Scenes/Media/LatexScene.h"
#include "../Scenes/Common/CompositeScene.h"
#include "../Core/Smoketest.h"
#include "../IO/SurgeXT.h"
#include "../IO/Writer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

using namespace std;

namespace {

string to_lower_copy(string value) {
    transform(value.begin(), value.end(), value.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return value;
}

vector<SurgeXTPatchInfo> pick_interesting_patches(const vector<SurgeXTPatchInfo>& patches) {
    vector<SurgeXTPatchInfo> preferred;
    for (const auto& patch : patches) {
        const string lower_name = to_lower_copy(patch.name);
        const string lower_category = to_lower_copy(patch.category);

        if (lower_category.find("pad") != string::npos ||
            lower_category.find("lead") != string::npos ||
            lower_category.find("plucked") != string::npos ||
            lower_name.find("pad") != string::npos ||
            lower_name.find("lead") != string::npos ||
            lower_name.find("pluck") != string::npos) {
            preferred.push_back(patch);
        }
    }
    if (!preferred.empty()) {
        return preferred;
    }
    return patches;
}

const char* read_env(const char* name) {
    const char* value = getenv(name);
    if (!value || !*value) {
        return nullptr;
    }
    return value;
}

void render_placeholder(const string& message) {
    cout << "SurgeXTDemo: " << message << endl;

    CompositeScene stage;
    auto title = make_shared<LatexScene>(
        "\\textbf{Surge XT demo}\\\\\\text{" + message + "}",
        0.9,
        vec2(0.9, 0.24)
    );
    stage.add_scene(title, "title", vec2(0.5, 0.28));
    stage_macroblock(SilenceBlock(1), 1);
    stage.render_microblock();
}

void maybe_print_patch_catalog(const vector<SurgeXTPatchInfo>& patches) {
    if (!read_env("SWAPTUBE_SURGE_XT_LIST_PATCHES")) {
        return;
    }

    map<string, int> counts_by_category;
    for (const auto& patch : patches) {
        counts_by_category[patch.category.empty() ? "(uncategorized)" : patch.category]++;
    }

    cout << "SurgeXTDemo: patch catalog contains " << patches.size() << " patches across "
         << counts_by_category.size() << " categories." << endl;
    for (const auto& [category, count] : counts_by_category) {
        cout << "  [" << category << "] " << count << endl;
    }

    const int preview_count = min<int>(patches.size(), 80);
    for (int i = 0; i < preview_count; ++i) {
        const auto& patch = patches[i];
        cout << "  #" << patch.ordered_index << " [" << patch.category << "] " << patch.name << endl;
    }
}

vector<SurgeXTPatchInfo> filter_patches_by_query(const vector<SurgeXTPatchInfo>& patches, const string& query) {
    if (query.empty()) {
        return patches;
    }

    const string lowered_query = to_lower_copy(query);
    vector<SurgeXTPatchInfo> filtered;
    for (const auto& patch : patches) {
        const string haystack =
            to_lower_copy(patch.category + " " + patch.name + " " + patch.path);
        if (haystack.find(lowered_query) != string::npos) {
            filtered.push_back(patch);
        }
    }
    return filtered;
}

}

void render_video() {
    if (!SurgeXT::available()) {
        render_placeholder("optional support is disabled");
        return;
    }

    SurgeXT surge(get_audio_samplerate_hz());
    const vector<SurgeXTPatchInfo> all_patches = surge.list_patches();
    if (all_patches.empty()) {
        render_placeholder("patch database is empty");
        return;
    }

    maybe_print_patch_catalog(all_patches);

    const char* patch_query_env = read_env("SWAPTUBE_SURGE_XT_PATCH_QUERY");
    const char* patch_path_env = read_env("SWAPTUBE_SURGE_XT_PATCH_PATH");
    if (patch_path_env) {
        if (!surge.load_patch_by_path(patch_path_env, "Swaptube")) {
            render_placeholder("requested patch path did not load");
            return;
        }
        cout << "SurgeXTDemo: loaded patch by path " << patch_path_env << endl;
    } else {
        vector<SurgeXTPatchInfo> filtered = patch_query_env
            ? filter_patches_by_query(all_patches, patch_query_env)
            : all_patches;
        if (filtered.empty()) {
            render_placeholder("no patches matched the current query");
            return;
        }
        const vector<SurgeXTPatchInfo> interesting = pick_interesting_patches(filtered);
        const SurgeXTPatchInfo chosen = interesting.front();
        surge.load_patch(chosen.ordered_index);
        cout << "SurgeXTDemo: selected patch [" << chosen.category << "] " << chosen.name << endl;
    }

    CompositeScene stage;
    auto title = make_shared<LatexScene>(
        "\\textbf{Surge XT headless demo}\\\\\\text{Patch loaded and rendered from C++}",
        0.95,
        vec2(0.8, 0.22)
    );
    stage.add_scene(title, "title", vec2(0.5, 0.25));

    vector<sample_t> left;
    vector<sample_t> right;
    const int sr = get_audio_samplerate_hz();

    surge.set_macro(1, 0.15f);
    surge.set_mod_wheel(0.05f);
    surge.note_on(48, 110);
    surge.render_samples(sr, left, right);

    vector<sample_t> left_next;
    vector<sample_t> right_next;
    surge.set_macro(1, 0.70f);
    surge.set_mod_wheel(0.45f);
    surge.render_samples(sr, left_next, right_next);
    left.insert(left.end(), left_next.begin(), left_next.end());
    right.insert(right.end(), right_next.begin(), right_next.end());

    surge.set_pitch_bend(0.20f);
    surge.set_macro(2, 0.65f);
    surge.render_samples(sr / 2, left_next, right_next);
    left.insert(left.end(), left_next.begin(), left_next.end());
    right.insert(right.end(), right_next.begin(), right_next.end());

    surge.note_off(48);
    surge.set_pitch_bend(0.0f);
    surge.render_samples(sr / 2, left_next, right_next);
    left.insert(left.end(), left_next.begin(), left_next.end());
    right.insert(right.end(), right_next.begin(), right_next.end());

    stage_macroblock(GeneratedBlock(left, right), 1);
    stage.render_microblock();
}
