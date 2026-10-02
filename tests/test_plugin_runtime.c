#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/dsp/plugin_api_v1.h"
#define MOVE_INFO_NO_READER
#include "../src/dsp/move_info.h"

static int pass_count;
static int fail_count;

#define CHECK(cond, msg) do { \
    if (cond) pass_count++; \
    else { fail_count++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static void quiet_log(const char *msg) { (void)msg; }
static int stopped_status(void) { return MOVE_CLOCK_STATUS_STOPPED; }
static float g_host_bpm = 99.0f;
static float test_bpm(void) { return g_host_bpm; }
static float g_move_bpm = 137.0f;
static int g_move_info_valid = 1;
__attribute__((visibility("default")))
int schwung_move_info(move_info_t *out, size_t cap)
{
    if (!out || cap < sizeof(*out)) return 0;
    memset(out, 0, sizeof(*out));
    out->size = sizeof(*out);
    out->version = MOVE_INFO_VERSION;
    out->valid = g_move_info_valid;
    out->tempo = g_move_bpm;
    return 1;
}
static double g_beat_position = -1.0;
static double test_beat_position(void) { return g_beat_position; }

static int buffer_is_silent(const int16_t *buf, int count) {
    for (int i = 0; i < count; i++) if (buf[i] != 0) return 0;
    return 1;
}

static int buffers_equal(const int16_t *a, const int16_t *b, int count) {
    for (int i = 0; i < count; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void send_clock_ticks(plugin_api_v2_t *api, void *instance,
                             int ticks, int16_t *audio) {
    const uint8_t clock = 0xF8;
    for (int i = 0; i < ticks; i++) {
        api->on_midi(instance, &clock, 1, MOVE_MIDI_SOURCE_HOST);
        api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <native-dsp.so> <sample.wav> <alternate.wav>\n", argv[0]);
        return 2;
    }

    char temp_dir[] = "/tmp/breakbeat-runtime.XXXXXX";
    CHECK(mkdtemp(temp_dir) != NULL, "create temporary module directory");
    char presets_dir[512];
    snprintf(presets_dir, sizeof(presets_dir), "%s/presets", temp_dir);
    CHECK(mkdir(presets_dir, 0700) == 0, "create preset directory");

    char preset_path[640];
    snprintf(preset_path, sizeof(preset_path), "%s/1_test.json", presets_dir);
    FILE *preset = fopen(preset_path, "w");
    CHECK(preset != NULL, "create test preset");
    if (!preset) return 2;
    fprintf(preset,
            "{\"A_sample_path\":\"%s\",\"A_sample_length\":\"4 bars\","
            "\"B_sample_path\":\"%s\",\"B_sample_length\":\"2 bars\","
            "\"phrase\":\"Off\"}\n",
            argv[2], argv[2]);
    fclose(preset);

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(handle != NULL, "load native plugin");
    if (!handle) {
        fprintf(stderr, "%s\n", dlerror());
        return 2;
    }

    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(
        handle, MOVE_PLUGIN_INIT_V2_SYMBOL);
    CHECK(init != NULL, "find v2 plugin entry point");

    host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    host.log = quiet_log;
    host.get_clock_status = stopped_status;
    host.get_bpm = test_bpm;
    host.get_beat_position = test_beat_position;

    plugin_api_v2_t *api = init(&host);
    CHECK(api && api->create_instance && api->render_block,
          "plugin exposes required v2 callbacks");
    void *instance = api->create_instance(temp_dir, "{}");
    CHECK(instance != NULL, "create plugin instance");

    char tempo[32];
    api->get_param(instance, "tempo_bpm", tempo, sizeof(tempo));
    CHECK(strncmp(tempo, "137.000", 7) == 0,
          "instance stores the Set tempo instead of measured clock BPM");

    int16_t audio[MOVE_FRAMES_PER_BLOCK * 2];
    memset(audio, 1, sizeof(audio));

    /* This is the chain host's Set-load sequence. The first preset assignment
     * and subsequent state restore must not audition audio. */
    api->set_param(instance, "preset", "0");
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "preset restore stays silent while stopped");

    char state[2048];
    snprintf(state, sizeof(state),
             "{\"preset_index\":0,\"sample_path\":\"%s\","
             "\"alt_sample_path\":\"%s\",\"length\":3,\"alt_length\":2,"
             "\"complexity\":0}",
             argv[2], argv[3]);
    api->set_param(instance, "state", state);
    memset(audio, 1, sizeof(audio));
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "state restore cancels preview while stopped");

    const uint8_t pad_slice_two[3] = {0x90, 38, 100};
    api->on_midi(instance, pad_slice_two, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(!buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "a pad auditions its slice with transport stopped");
    char pad_status[32];
    api->get_param(instance, "status", pad_status, sizeof(pad_status));
    CHECK(strncmp(pad_status, "A 2 ", 4) == 0,
          "stopped pad selects the corresponding slice");
    for (int i = 0; i < 512; i++)
        api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "stopped pad audition ends after one slice");

    const uint8_t pad_a_slice_zero[3] = {0x90, 36, 100};
    api->on_midi(instance, pad_a_slice_zero, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    int16_t a_pad_audio[MOVE_FRAMES_PER_BLOCK * 2];
    memcpy(a_pad_audio, audio, sizeof(a_pad_audio));
    const uint8_t pad_b_slice_zero[3] = {0x90, 44, 100};
    api->on_midi(instance, pad_b_slice_zero, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", pad_status, sizeof(pad_status));
    CHECK(strncmp(pad_status, "B 0 ", 4) == 0 &&
          !buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "ninth pad auditions B slice zero while stopped");
    int16_t b_pad_audio[MOVE_FRAMES_PER_BLOCK * 2];
    memcpy(b_pad_audio, audio, sizeof(b_pad_audio));
    CHECK(!buffers_equal(a_pad_audio, audio, MOVE_FRAMES_PER_BLOCK * 2),
          "ninth pad reads B sample audio rather than A sample audio");
    api->set_param(instance, "state", state);
    api->on_midi(instance, pad_slice_two, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", pad_status, sizeof(pad_status));
    CHECK(strncmp(pad_status, "A 2 ", 4) == 0,
          "state restore after a B pad keeps A available");

    /* Set metadata can arrive after the instance is created. A later pad
     * press must refresh the host tempo even when the model is unavailable. */
    g_move_info_valid = 0;
    g_host_bpm = 82.0f;
    api->on_midi(instance, pad_slice_two, 3, MOVE_MIDI_SOURCE_EXTERNAL);
    char pad_tempo[32];
    api->get_param(instance, "tempo_bpm", pad_tempo, sizeof(pad_tempo));
    CHECK(strncmp(pad_tempo, "82.000", 6) == 0,
          "stopped pad refreshes host tempo after loading");
    g_move_info_valid = 1;
    g_host_bpm = 99.0f;
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);

    /* A uses two bars while B uses one. A B pad must sound at its own
     * stopped-audition speed even when A owns the running clock grid. */
    const uint8_t trial_start = 0xFA, trial_stop = 0xFC, trial_clock = 0xF8;
    api->on_midi(instance, &trial_start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &trial_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    send_clock_ticks(api, instance, 21, audio);
    api->on_midi(instance, pad_b_slice_zero, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    send_clock_ticks(api, instance, 3, audio);
    api->get_param(instance, "status", pad_status, sizeof(pad_status));
    CHECK(strncmp(pad_status, "B 0 ", 4) == 0,
          "B pad lands on the A grid despite different loop lengths");
    CHECK(buffers_equal(b_pad_audio, audio, MOVE_FRAMES_PER_BLOCK * 2),
          "B pad plays at B's own speed during A playback");
    send_clock_ticks(api, instance, 24, audio);
    api->get_param(instance, "status", pad_status, sizeof(pad_status));
    CHECK(strncmp(pad_status, "A 2 ", 4) == 0,
          "two-bar A grid resumes after a one-bar B pad slice");
    api->on_midi(instance, &trial_stop, 1, MOVE_MIDI_SOURCE_HOST);

    char saved[2048];
    int saved_len = api->get_param(instance, "state", saved, sizeof(saved));
    CHECK(saved_len > 0 && strstr(saved, "\"preset_index\":0") != NULL,
          "state round-trips preset_index");
    CHECK(saved_len > 0 && strstr(saved, "\"alt_length\":2") != NULL,
          "state round-trips B length");
    api->set_param(instance, "pitch_lock", "1");
    api->set_param(instance, "grain_fx", "100");
    api->set_param(instance, "grain_cycle_ms", "20");
    api->set_param(instance, "stretch_chance", "65");
    api->set_param(instance, "stretch_length_min", "50");
    api->set_param(instance, "stretch_length_max", "80");
    api->set_param(instance, "stretch_slice_min", "14");
    api->set_param(instance, "stretch_slice_max", "43");
    api->set_param(instance, "stretch_pitch_min", "25");
    api->set_param(instance, "stretch_pitch_max", "75");
    saved_len = api->get_param(instance, "state", saved, sizeof(saved));
    CHECK(saved_len > 0 && strstr(saved, "\"pitch_lock\":1") &&
          strstr(saved, "\"grain_fx\":100") &&
          strstr(saved, "\"grain_cycle_ms\":20") &&
          strstr(saved, "\"stretch_chance\":65") &&
          strstr(saved, "\"stretch_length_min\":50") &&
          strstr(saved, "\"stretch_length_max\":80") &&
          strstr(saved, "\"stretch_slice_min\":14") &&
          strstr(saved, "\"stretch_slice_max\":43") &&
          strstr(saved, "\"stretch_pitch_min\":25") &&
          strstr(saved, "\"stretch_pitch_max\":75"),
          "stretch controls are saved in song state");
    api->set_param(instance, "pitch_lock", "0");
    api->set_param(instance, "grain_fx", "0");
    api->set_param(instance, "grain_cycle_ms", "40");
    api->set_param(instance, "stretch_chance", "0");
    api->set_param(instance, "stretch_length_min", "0");
    api->set_param(instance, "stretch_pitch_max", "0");

    char hierarchy[8192];
    int hierarchy_len = api->get_param(instance, "ui_hierarchy", hierarchy,
                                       sizeof(hierarchy));
    CHECK(hierarchy_len > 0 && strstr(hierarchy, "\"pitch_lock\"") &&
          strstr(hierarchy, "\"grain_fx\"") &&
          strstr(hierarchy, "\"level\":\"stretch\"") &&
          strstr(hierarchy, "\"stretch_chance\"") &&
          strstr(hierarchy, "\"level\":\"retrig\"") &&
          strstr(hierarchy, "\"name\":\"Main\"") &&
          hierarchy[hierarchy_len - 1] == '}',
          "stretch controls appear in complete device UI hierarchy");
    const char *retrig_page = strstr(hierarchy, "\"retrig\":{\"name\":\"Retrig\"");
    const char *anchors_page = strstr(hierarchy, "\"anchors\":{\"name\":\"Anchors\"");
    CHECK(retrig_page && !strstr(retrig_page, "\"preset\"") &&
          strstr(retrig_page, "\"stretch\":{\"name\":\"Stretch\""),
          "named Retrig page has no Preset control and precedes Stretch");
    CHECK(anchors_page && retrig_page && anchors_page < retrig_page &&
          strstr(hierarchy, "\"knobs\":[\"A_sample_path\",\"A_sample_length\",\"B_sample_path\",\"B_sample_length\",\"B_chance\",\"complexity\",\"phrase\",\"status\"]") &&
          !strstr(hierarchy, "\"key\":\"save_preset\"") &&
          !strstr(hierarchy, "\"key\":\"preset\""),
          "Main shows status and Anchors has its own named page without custom presets");
    char chain_params[8192];
    api->get_param(instance, "chain_params", chain_params, sizeof(chain_params));
    CHECK(strstr(chain_params, "\"key\":\"stretch_length_min\",\"type\":\"int\",\"min\":0,\"max\":100,\"name\":\"Length Minimum\"") &&
          strstr(chain_params, "\"name\":\"Pitch Maximum\"") &&
          strstr(chain_params, "\"name\":\"Grain Cycle\""),
          "stretch controls publish readable names for touch feedback");
    api->set_param(instance, "state", saved);
    char stretch_value[16];
    api->get_param(instance, "grain_cycle_ms", stretch_value, sizeof(stretch_value));
    CHECK(strcmp(stretch_value, "20") == 0,
          "song state restores stretch settings");
    api->get_param(instance, "stretch_chance", stretch_value, sizeof(stretch_value));
    CHECK(strcmp(stretch_value, "65") == 0,
          "song state restores stretch probability");
    api->get_param(instance, "stretch_pitch_max", stretch_value, sizeof(stretch_value));
    CHECK(strcmp(stretch_value, "75") == 0,
          "song state restores pitch range");
    api->set_param(instance, "pitch_lock", "0");
    api->set_param(instance, "grain_fx", "0");
    api->set_param(instance, "grain_cycle_ms", "40");
    api->set_param(instance, "stretch_chance", "0");
    api->set_param(instance, "stretch_length_min", "0");
    api->set_param(instance, "stretch_pitch_max", "0");

    snprintf(state, sizeof(state),
             "{\"preset_index\":0,\"sample_path\":\"%s\"," 
             "\"alt_sample_path\":\"%s\",\"length\":2,\"alt_length\":3,"
             "\"complexity\":0,\"anchor\":0,\"roll\":0,"
             "\"retrig_2x\":0,\"retrig_3x\":0,\"retrig_4x\":0,\"retrig_8x\":0,"
             "\"phrase\":2,\"swap_prob\":100}", argv[2], argv[2]);
    api->set_param(instance, "state", state);

    const uint8_t start = 0xFA;
    const uint8_t stop = 0xFC;
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    memset(audio, 0, sizeof(audio));
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "Start pre-roll stays silent before the real downbeat");
    const uint8_t first_clock = 0xF8;
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(!buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "first clock releases slice zero on the downbeat");

    char status[32];
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "MIDI Start begins A on slice zero");

    /* A note arriving after the downbeat block has rendered must wait for
     * the next boundary, even if no further MIDI clock tick has arrived. */
    const uint8_t recorded_slice_three[3] = {0x90, 39, 100};
    api->on_midi(instance, recorded_slice_three, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "late note cannot retrigger off the grid");
    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 3 ", 4) == 0,
          "late note lands on the next slice boundary");
    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 2 ", 4) == 0,
          "automatic slicing resumes one boundary after the note");

    send_clock_ticks(api, instance, 9, audio);
    const uint8_t early_pad_slice_six[3] = {0x90, 42, 100};
    api->on_midi(instance, early_pad_slice_six, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 2 ", 4) == 0,
          "early pad waits while the current slice finishes");
    send_clock_ticks(api, instance, 3, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 6 ", 4) == 0,
          "early pad lands exactly on the next slice boundary");
    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 4 ", 4) == 0,
          "automatic break resumes one full slice after quantized pad");

    send_clock_ticks(api, instance, 9, audio);
    const uint8_t early_b_pad_slice_two[3] = {0x90, 46, 100};
    api->on_midi(instance, early_b_pad_slice_two, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 4 ", 4) == 0,
          "early B pad waits for the next slice boundary");
    send_clock_ticks(api, instance, 3, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "B 2 ", 4) == 0,
          "pad eleven plays B slice two on the next boundary");
    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 6 ", 4) == 0,
          "A break resumes after one full B pad slice");

    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, recorded_slice_three, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 3 ", 4) == 0,
          "note on the first downbeat survives transport reset");

    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, recorded_slice_three, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 3 ", 4) == 0,
          "note after first clock still claims the unrendered downbeat");

    api->on_midi(instance, &stop, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 1 ", 4) == 0,
          "next Play without notes resumes automatic slicing");
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);

    api->set_param(instance, "A_sample_length", "1"); /* 1/2 bar */
    send_clock_ticks(api, instance, 6, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 1 ", 4) == 0,
          "live A length change takes effect at the next new trigger interval");
    api->set_param(instance, "A_sample_length", "2"); /* restore 1 bar */
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);

    g_move_bpm = 142.0f;
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "tempo_bpm", tempo, sizeof(tempo));
    CHECK(strncmp(tempo, "142.000", 7) == 0,
          "stored tempo updates when the user changes the Set BPM");

    /* Move can defer persisting Song.abl while transport is running. Its MIDI
     * clock changes immediately, so live BPM must win for playback rate. */
    g_move_bpm = 74.0f;
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "tempo_bpm", tempo, sizeof(tempo));
    CHECK(strncmp(tempo, "74.000", 6) == 0,
          "running playback rate follows a live tempo change without restart");
    g_move_bpm = 142.0f;

    api->on_midi(instance, &stop, 1, MOVE_MIDI_SOURCE_HOST);
    g_move_bpm = 96.0f;
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->get_param(instance, "tempo_bpm", tempo, sizeof(tempo));
    CHECK(strncmp(tempo, "96.000", 6) == 0,
          "transport Start refreshes a tempo changed while stopped");
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    g_move_bpm = 142.0f;

    int16_t previous_audio[MOVE_FRAMES_PER_BLOCK * 2];
    memcpy(previous_audio, audio, sizeof(audio));
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(!buffers_equal(previous_audio, audio, MOVE_FRAMES_PER_BLOCK * 2),
          "audio cursor remains continuous if the host beat is briefly unchanged");

    send_clock_ticks(api, instance, 12, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 1 ", 4) == 0,
          "twelfth MIDI clock advances to A slice one");

    send_clock_ticks(api, instance, 84, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "first beat of bar two returns to A slice zero");

    send_clock_ticks(api, instance, 96, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "first beat of bar three remains A slice zero");

    send_clock_ticks(api, instance, 95, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(status[0] == 'A', "A owns all of the first three bars");

    send_clock_ticks(api, instance, 1, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "B 0 ", 4) == 0,
          "B begins on bar four regardless of its two-bar source length");

    api->on_midi(instance, pad_slice_two, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "B 0 ", 4) == 0,
          "A pad waits for the next boundary during a B fill");

    api->set_param(instance, "B_sample_length", "1"); /* 1/2 bar */
    send_clock_ticks(api, instance, 6, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 2 ", 4) == 0,
          "A pad interrupts B on its next clocked slice");
    send_clock_ticks(api, instance, 6, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "B 2 ", 4) == 0,
          "B fill resumes after the manual A slice");
    api->set_param(instance, "B_sample_length", "3"); /* restore 2 bars */

    send_clock_ticks(api, instance, 83, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(status[0] == 'B', "B remains active through the final phrase bar");

    send_clock_ticks(api, instance, 1, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "A returns on the next four-bar phrase boundary");

    /* A Stop during B must not make the next song run begin on B. */
    send_clock_ticks(api, instance, 288, audio);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(status[0] == 'B', "second phrase reaches its one-bar B fill");

    api->on_midi(instance, &stop, 1, MOVE_MIDI_SOURCE_HOST);
    memset(audio, 1, sizeof(audio));
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "Stop silences the next block");

    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(instance, "status", status, sizeof(status));
    CHECK(strncmp(status, "A 0 ", 4) == 0,
          "restarting after a B fill always begins with A slice zero");

    api->on_midi(instance, &stop, 1, MOVE_MIDI_SOURCE_HOST);

    const uint8_t clock = 0xF8;
    for (int i = 0; i < 192; i++)
        api->on_midi(instance, &clock, 1, MOVE_MIDI_SOURCE_HOST);
    memset(audio, 1, sizeof(audio));
    api->render_block(instance, audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffer_is_silent(audio, MOVE_FRAMES_PER_BLOCK * 2),
          "clock while stopped cannot restart playback");

    /* Regression: the Movy filepath browser can commit A while transport is
     * running. It must load off-thread and survive the next Stop/Start. */
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->set_param(instance, "A_sample_path", argv[3]);
    usleep(250000);
    api->on_midi(instance, &stop, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(instance, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    int16_t switched_audio[MOVE_FRAMES_PER_BLOCK * 2];
    api->render_block(instance, switched_audio, MOVE_FRAMES_PER_BLOCK);

    preset = fopen(preset_path, "w");
    CHECK(preset != NULL, "rewrite reference preset for alternate sample");
    if (preset) {
        fprintf(preset,
                "{\"A_sample_path\":\"%s\",\"A_sample_length\":\"1 bar\","
                "\"B_sample_path\":\"%s\",\"B_sample_length\":\"2 bars\","
                "\"phrase\":\"Off\"}\n", argv[3], argv[2]);
        fclose(preset);
    }
    void *reference = api->create_instance(temp_dir, "{}");
    CHECK(reference != NULL, "create alternate-sample reference instance");
    int16_t expected_audio[MOVE_FRAMES_PER_BLOCK * 2];
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(buffers_equal(switched_audio, expected_audio,
                        MOVE_FRAMES_PER_BLOCK * 2),
          "A filepath selection while running changes the mapped audio");

    /* Render identical clean starts with each stretch mode. This catches a
     * control that saves correctly but never reaches the audio path. */
    int16_t dry_early[MOVE_FRAMES_PER_BLOCK * 2];
    int16_t dry_late[MOVE_FRAMES_PER_BLOCK * 2];
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    for (int i = 0; i < 40; i++) {
        api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
        if (i == 5) memcpy(dry_early, expected_audio, sizeof(dry_early));
    }
    memcpy(dry_late, expected_audio, sizeof(dry_late));

    api->set_param(reference, "pitch_lock", "1");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    for (int i = 0; i <= 5; i++)
        api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(!buffers_equal(dry_early, expected_audio, MOVE_FRAMES_PER_BLOCK * 2),
          "Pitch Lock changes audio at a non-native tempo");

    api->set_param(reference, "pitch_lock", "0");
    api->set_param(reference, "grain_fx", "100");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    for (int i = 0; i < 40; i++)
        api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    CHECK(!buffers_equal(dry_late, expected_audio, MOVE_FRAMES_PER_BLOCK * 2),
          "Grain FX changes audio independently of Pitch Lock");

    api->set_param(reference, "grain_fx", "0");
    api->set_param(reference, "complexity", "0");
    api->set_param(reference, "anchor", "0");
    api->set_param(reference, "roll", "0");
    api->set_param(reference, "stretch_chance", "0");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 0 1x") == 0,
          "zero stretch chance leaves the automatic break alone");

    api->set_param(reference, "stretch_chance", "100");
    api->set_param(reference, "stretch_length_min", "0");
    api->set_param(reference, "stretch_length_max", "0");
    api->set_param(reference, "stretch_slice_min", "0");
    api->set_param(reference, "stretch_slice_max", "0");
    api->set_param(reference, "stretch_pitch_min", "50");
    api->set_param(reference, "stretch_pitch_max", "50");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 0 ST") == 0,
          "stretch status identifies sample, slice, and effect");
    char stretch_info[32];
    api->get_param(reference, "stretch_info", stretch_info, sizeof(stretch_info));
    CHECK(strcmp(stretch_info, "L2 S1 P0") == 0,
          "minimum controls select 2x for one slice at original pitch");
    send_clock_ticks(api, reference, 12, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 1 ST") == 0,
          "one-slice stretch returns control at the next boundary");

    api->set_param(reference, "stretch_length_min", "21");
    api->set_param(reference, "stretch_length_max", "21");
    api->set_param(reference, "stretch_slice_min", "43");
    api->set_param(reference, "stretch_slice_max", "43");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(reference, "stretch_info", stretch_info, sizeof(stretch_info));
    CHECK(strcmp(stretch_info, "L5 S4 P0") == 0,
          "arbitrary 5x length and four-slice hold are independent");
    send_clock_ticks(api, reference, 36, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 0 ST") == 0,
          "four-slice stretch holds through three later boundaries");
    send_clock_ticks(api, reference, 12, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 4 ST") == 0,
          "automatic slicing resumes after the four-slice hold");

    api->set_param(reference, "stretch_length_min", "0");
    api->set_param(reference, "stretch_length_max", "100");
    api->set_param(reference, "stretch_slice_min", "0");
    api->set_param(reference, "stretch_slice_max", "43");
    api->set_param(reference, "stretch_pitch_min", "0");
    api->set_param(reference, "stretch_pitch_max", "100");
    int saw_five = 0, saw_sixteen = 0, saw_four = 0, saw_low_pitch = 0;
    for (int trial = 0; trial < 128; trial++) {
        api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
        api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
        api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
        api->get_param(reference, "stretch_info", stretch_info, sizeof(stretch_info));
        int length = 0, slices = 0, pitch = 0;
        sscanf(stretch_info, "L%d S%d P%d", &length, &slices, &pitch);
        CHECK(length >= 2 && length <= 16 && slices >= 1 && slices <= 4 &&
              pitch >= -12 && pitch <= 12,
              "random stretch stays within length, slice, and pitch bounds");
        if (length == 5) saw_five = 1;
        if (length == 16) saw_sixteen = 1;
        if (slices == 4) saw_four = 1;
        if (pitch < 0) saw_low_pitch = 1;
    }
    CHECK(saw_five && saw_sixteen && saw_four && saw_low_pitch,
          "random ranges include non-power-of-two lengths and signed pitch");

    api->set_param(reference, "stretch_length_min", "100");
    api->set_param(reference, "stretch_length_max", "100");
    api->set_param(reference, "stretch_slice_min", "100");
    api->set_param(reference, "stretch_slice_max", "100");
    api->set_param(reference, "stretch_pitch_min", "0");
    api->set_param(reference, "stretch_pitch_max", "0");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    api->get_param(reference, "stretch_info", stretch_info, sizeof(stretch_info));
    CHECK(strcmp(stretch_info, "L16 S8 P-12") == 0,
          "maximum and minimum endpoints map to 16x, eight slices, and -12 semitones");
    send_clock_ticks(api, reference, 9, expected_audio);
    const uint8_t interrupt_pad[3] = {0x90, 41, 100};
    api->on_midi(reference, interrupt_pad, 3, MOVE_MIDI_SOURCE_INTERNAL);
    send_clock_ticks(api, reference, 3, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 5 1x") == 0,
          "pad note replaces a long stretch on the next grid boundary");
    send_clock_ticks(api, reference, 12, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "A 2 ST") == 0,
          "automatic stretch resumes after the manual pad's slice");

    api->set_param(reference, "A_sample_length", "3"); /* 2 bars */
    api->set_param(reference, "phrase", "1"); /* 2 bars */
    api->set_param(reference, "B_chance", "100");
    api->on_midi(reference, &start, 1, MOVE_MIDI_SOURCE_HOST);
    api->on_midi(reference, &first_clock, 1, MOVE_MIDI_SOURCE_HOST);
    api->render_block(reference, expected_audio, MOVE_FRAMES_PER_BLOCK);
    send_clock_ticks(api, reference, 96, expected_audio);
    api->get_param(reference, "status", status, sizeof(status));
    CHECK(strcmp(status, "B 0 ST") == 0,
          "B phrase boundary interrupts an eight-slice A stretch");
    api->set_param(reference, "state",
                   "{\"stretch_2x\":0,\"stretch_3x\":0,"
                   "\"stretch_4x\":0,\"stretch_8x\":100}");
    char migrated[16];
    api->get_param(reference, "stretch_chance", migrated, sizeof(migrated));
    CHECK(strcmp(migrated, "100") == 0,
          "old stretch settings retain their overall chance");
    api->get_param(reference, "stretch_slice_max", migrated, sizeof(migrated));
    CHECK(strcmp(migrated, "14") == 0,
          "old long stretch settings migrate to a two-slot limit");
    api->destroy_instance(reference);

    api->destroy_instance(instance);
    dlclose(handle);
    unlink(preset_path);
    rmdir(presets_dir);
    rmdir(temp_dir);

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
