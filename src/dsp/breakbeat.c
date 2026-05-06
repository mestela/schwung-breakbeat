#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "plugin_api_v1.h"
#include "slice_select.h"
#include <time.h>

/* WAV audio format codes */
#define WAV_FORMAT_PCM   1
#define WAV_FORMAT_FLOAT 3

#define BB_PATH_MAX 512

typedef struct {
    int preset_idx;
    /* All paths stored as absolute. relative inputs get resolved via module_dir. */
    char alt_sample_path[BB_PATH_MAX];   /* alt sample for phrase swap */
    char main_sample_path[BB_PATH_MAX];  /* main loop user choice; restored at top of phrase */
    char pending_sample_path[BB_PATH_MAX]; /* deferred load; "" = nothing pending */
    char module_dir[BB_PATH_MAX];        /* base for relative paths */
    float length;
    float complexity;
    float anchor;
    float roll;
    int   phrase_bars;
    float fill;
    int   bar_counter;
    int   reseed_pending;

    // WAV file state
    int fd;
    void *map;
    size_t map_size;
    void *data;
    uint32_t total_frames;
    float play_pos;
    int num_channels;
    float original_bpm;
    int audio_format;
    int bits_per_sample;
    int playing;

    // Slice state
    uint32_t slice_starts[8];
    uint32_t slice_lengths[8];
    int current_slice;
    int clock_counter;
    float retrigger_prob;
    int sub_slice_active;
    int sub_slice_counter;
    int retrigger_divisions;
    int retrigger_rate_idx;

    uint64_t sample_counter;
    uint64_t last_clock_samples;
    float calculated_bpm;
    float swap_prob;

} breakbeat_t;

static const host_api_v1_t *g_host = NULL;
static int g_num_presets = 3; // Stub

typedef struct {
    char name[64];
    const char *sample_path;     /* relative to module_dir, or absolute */
    const char *alt_sample_path; /* relative to module_dir, or absolute */
    float length;
    float complexity;
    float retrigger_prob;
    int retrigger_divisions;
    float anchor;
    float roll;
    int phrase_bars;
    float fill;
} preset_t;

static preset_t g_presets[] = {
    /* name, sample, alt_sample, length, cplx, retrig, ratediv, anchor, roll, phrase, fill */
    {"Calm",    "/data/UserData/breakbeat-samples/Built-in/amen01.wav",
                "/data/UserData/breakbeat-samples/Built-in/amen09.wav",  0.5f,  0.18f, 0.16f, 2,  0.80f, 0.70f, 4, 0.50f},
    {"Mid",     "/data/UserData/breakbeat-samples/Built-in/amen09.wav",
                "/data/UserData/breakbeat-samples/Built-in/amen18.wav",  0.5f,  0.56f, 0.08f, 4,  0.60f, 0.50f, 4, 0.60f},
    {"Frantic", "/data/UserData/breakbeat-samples/Built-in/sesame.wav",
                "/data/UserData/breakbeat-samples/Built-in/think.wav",   0.25f, 0.80f, 0.11f, 4,  0.30f, 0.20f, 2, 0.80f}
};

static const char *g_loop_names[] = {
    "amen01", "amen09", "amen18", "amen19", "amen20", "apache", "do", "eeloil", "fireeater", "funkydrummer", "groove", "hungup_0", "king", "kool", "mechanicalman", "neworleans", "riffin", "ripple", "sesame", "sport", "squib", "think", "useme"
};

static float bb_rand(void *ctx) {
    (void)ctx;
    return (float)rand() / (float)RAND_MAX;
}

static void wp_log(const char *msg) {
    if (g_host && g_host->log) g_host->log(msg);
}

/* JSON helpers for state parsing */
static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len < 1) return 0;
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return 0;
    const char *colon = strchr(pos, ':');
    if (!colon) return 0;
    while (*colon && (*colon == ':' || *colon == ' ' || *colon == '\t')) colon++;
    if (*colon != '"') return 0;
    colon++;
    const char *end = strchr(colon, '"');
    if (!end) return 0;
    int len = (int)(end - colon);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, colon, len);
    out[len] = '\0';
    return len;
}

static int json_get_int(const char *json, const char *key, int *out) {
    if (!json || !key || !out) return 0;
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return 0;
    const char *colon = strchr(pos, ':');
    if (!colon) return 0;
    colon++;
    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
    *out = atoi(colon);
    return 1;
}

static void close_file(breakbeat_t *wp) {
    if (wp->map && wp->map != MAP_FAILED) {
        munmap(wp->map, wp->map_size);
    }
    if (wp->fd >= 0) {
        close(wp->fd);
    }
    wp->fd = -1;
    wp->map = NULL;
    wp->map_size = 0;
    wp->data = NULL;
    wp->total_frames = 0;
    wp->play_pos = 0;
    wp->num_channels = 0;
    wp->audio_format = 0;
    wp->bits_per_sample = 0;
    wp->playing = 0;
}

/* True if extension (case-insensitive) matches .wav */
static int has_wav_ext(const char *path) {
    if (!path) return 0;
    size_t n = strlen(path);
    if (n < 4) return 0;
    const char *e = path + n - 4;
    return (e[0] == '.')
        && (e[1] == 'w' || e[1] == 'W')
        && (e[2] == 'a' || e[2] == 'A')
        && (e[3] == 'v' || e[3] == 'V');
}

/* Resolve possibly-relative `path` against `module_dir` into `out`.
 * Absolute paths copy as-is; relative paths get prefixed with module_dir. */
static void resolve_sample_path(const char *module_dir, const char *path, char *out, size_t out_len) {
    if (!path || !path[0]) { if (out_len > 0) out[0] = '\0'; return; }
    if (path[0] == '/') {
        snprintf(out, out_len, "%s", path);
    } else if (module_dir && module_dir[0]) {
        snprintf(out, out_len, "%s/%s", module_dir, path);
    } else {
        snprintf(out, out_len, "%s", path);
    }
}

/* Create the filepath browser "portal" with symlinks to bundled samples and
 * to the user's sample library. Idempotent — safe to run on every instance
 * creation. Needed because Module Store installs unpack the tarball but
 * don't run install.sh, so the symlinks must be set up at runtime. */
#define BB_PORTAL_DIR        "/data/UserData/breakbeat-samples"
#define BB_USER_LIB_TARGET   "/data/UserData/UserLibrary/Samples"
#define BB_BUILTIN_LINK      BB_PORTAL_DIR "/Built-in"
#define BB_USER_LIB_LINK     BB_PORTAL_DIR "/User Library"

static void ensure_portal_exists(const char *module_dir) {
    if (mkdir(BB_PORTAL_DIR, 0755) != 0 && errno != EEXIST) {
        wp_log("breakbeat: could not create portal dir");
        /* Continue anyway — symlink calls will surface their own errors. */
    }

    char target[BB_PATH_MAX];
    snprintf(target, sizeof(target), "%s/samples", module_dir);

    /* Replace any pre-existing link to make sure it points where we want. */
    struct stat st;
    if (lstat(BB_BUILTIN_LINK, &st) == 0) (void)unlink(BB_BUILTIN_LINK);
    if (symlink(target, BB_BUILTIN_LINK) != 0 && errno != EEXIST) {
        wp_log("breakbeat: could not create Built-in symlink");
    }

    /* Only expose User Library if it actually exists on this device. */
    if (stat(BB_USER_LIB_TARGET, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (lstat(BB_USER_LIB_LINK, &st) == 0) (void)unlink(BB_USER_LIB_LINK);
        if (symlink(BB_USER_LIB_TARGET, BB_USER_LIB_LINK) != 0 && errno != EEXIST) {
            wp_log("breakbeat: could not create User Library symlink");
        }
    }
}

/* Open and validate a WAV file before destroying the previously-loaded one.
 * On any failure, returns -1 and leaves wp's existing sample state untouched. */
static int open_wav(breakbeat_t *wp, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        wp_log("breakbeat: failed to open file");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 44) {
        wp_log("breakbeat: file too small for WAV header");
        close(fd);
        return -1;
    }

    size_t map_size = (size_t)st.st_size;
    void *map = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        wp_log("breakbeat: mmap failed");
        close(fd);
        return -1;
    }

    const uint8_t *raw = (const uint8_t *)map;
    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) {
        wp_log("breakbeat: not a RIFF/WAVE file");
        munmap(map, map_size);
        close(fd);
        return -1;
    }

    uint32_t offset = 12;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint16_t bits_per_sample = 0;
    int found_fmt = 0;
    int found_data = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;

    while (offset + 8 <= map_size) {
        const uint8_t *chunk = raw + offset;
        uint32_t chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format    = chunk[8]  | (chunk[9]  << 8);
            num_channels    = chunk[10] | (chunk[11] << 8);
            bits_per_sample = chunk[22] | (chunk[23] << 8);
            found_fmt = 1;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = offset + 8;
            data_size = chunk_size;
            found_data = 1;
            break;
        }
        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++;
    }

    if (!found_fmt || !found_data) {
        wp_log("breakbeat: missing fmt or data chunk");
        munmap(map, map_size);
        close(fd);
        return -1;
    }

    /* WAVE_FORMAT_EXTENSIBLE (0xFFFE) lives in the subformat GUID; many DAWs
     * write 24-bit/32-bit PCM with this format code. Treat it as PCM. */
    int is_float = (audio_format == WAV_FORMAT_FLOAT);
    int is_pcm   = (audio_format == WAV_FORMAT_PCM) || (audio_format == 0xFFFE);
    int bytes_per_sample = 0;
    if (is_pcm) {
        if      (bits_per_sample == 16) bytes_per_sample = 2;
        else if (bits_per_sample == 24) bytes_per_sample = 3;
        else if (bits_per_sample == 32) bytes_per_sample = 4;
    } else if (is_float && bits_per_sample == 32) {
        bytes_per_sample = 4;
    }

    if (bytes_per_sample == 0 || num_channels == 0 || num_channels > 2) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "breakbeat: unsupported format (audio_format=%u, bits=%u, ch=%u)",
                 audio_format, bits_per_sample, num_channels);
        wp_log(buf);
        munmap(map, map_size);
        close(fd);
        return -1;
    }

    /* Validation passed — now destroy old and commit new. */
    close_file(wp);

    wp->fd = fd;
    wp->map = map;
    wp->map_size = map_size;
    /* Keep audio_format as the canonical "float vs PCM" hint for render_block. */
    wp->audio_format = is_float ? WAV_FORMAT_FLOAT : WAV_FORMAT_PCM;
    wp->bits_per_sample = bits_per_sample;
    wp->num_channels = num_channels;
    wp->data = (void *)(raw + data_offset);
    wp->total_frames = data_size / (num_channels * bytes_per_sample);
    wp->play_pos = 0;

    uint32_t slice_size = wp->total_frames / 8;
    for (int i = 0; i < 8; i++) {
        wp->slice_starts[i] = i * slice_size;
        wp->slice_lengths[i] = slice_size;
    }

    char logbuf[160];
    snprintf(logbuf, sizeof(logbuf),
             "breakbeat: loaded %u frames (%uch, %ubit, %s)",
             wp->total_frames, num_channels, bits_per_sample,
             is_float ? "float" : "PCM");
    wp_log(logbuf);

    return 0;
}

/* Load the sample at `path` (relative or absolute). Returns 0 on success. */
static int apply_sample_path(breakbeat_t *bb, const char *path) {
    if (!bb || !path || !path[0]) return -1;
    if (!has_wav_ext(path)) {
        char buf[BB_PATH_MAX + 64];
        snprintf(buf, sizeof(buf), "breakbeat: rejected non-.wav path: %s", path);
        wp_log(buf);
        return -1;
    }
    char resolved[1024];
    resolve_sample_path(bb->module_dir, path, resolved, sizeof(resolved));

    char log_buf[BB_PATH_MAX + 64];
    snprintf(log_buf, sizeof(log_buf), "breakbeat: applying sample %s", resolved);
    wp_log(log_buf);

    return open_wav(bb, resolved);
}

/* Legacy migration: old loop_idx (0..22) → "samples/<name>.wav". */
static int legacy_idx_to_path(int idx, char *out, int out_len) {
    if (idx < 0 || idx >= 23 || !out || out_len < 1) return -1;
    snprintf(out, out_len, "samples/%s.wav", g_loop_names[idx]);
    return 0;
}

static void* bb_create_instance(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    breakbeat_t *bb = calloc(1, sizeof(breakbeat_t));
    if (!bb) return NULL;

    bb->preset_idx = 0;
    bb->retrigger_prob = 0.0f;
    bb->sub_slice_active = 0;
    bb->sub_slice_counter = 0;
    bb->retrigger_divisions = 2;
    bb->retrigger_rate_idx = 0;
    bb->length = 1.0f;
    bb->sample_counter = 0;
    bb->last_clock_samples = 0;
    bb->calculated_bpm = 150.0f; // Default fallback
    bb->swap_prob = 0.0f;
    bb->complexity = 0.5f;
    bb->anchor = 0.0f;
    bb->roll = 0.0f;
    bb->phrase_bars = 0;
    bb->fill = 0.0f;
    bb->bar_counter = 0;
    bb->reseed_pending = 0;
    bb->fd = -1;

    /* Capture module_dir for relative-path resolution. */
    if (module_dir && module_dir[0]) {
        snprintf(bb->module_dir, sizeof(bb->module_dir), "%s", module_dir);
    } else {
        /* Fallback to the install path if host doesn't provide module_dir. */
        snprintf(bb->module_dir, sizeof(bb->module_dir),
                 "/data/UserData/schwung/modules/sound_generators/breakbeat");
    }

    /* Set up the filepath browser portal (Built-in + User Library symlinks). */
    ensure_portal_exists(bb->module_dir);

    /* Defaults point at the install-time portal symlinks, so the filepath
     * browser opens inside the Built-in samples folder on first use. */
    snprintf(bb->main_sample_path, sizeof(bb->main_sample_path),
             "/data/UserData/breakbeat-samples/Built-in/amen.wav");
    snprintf(bb->alt_sample_path,  sizeof(bb->alt_sample_path),
             "/data/UserData/breakbeat-samples/Built-in/amen09.wav");
    bb->pending_sample_path[0] = '\0';

    if (g_host && g_host->log) g_host->log("breakbeat: instance created");

    apply_sample_path(bb, bb->main_sample_path);
    bb->playing = 1; // Auto-play
    bb->original_bpm = 137.7f; // Approx for Amen

    return bb;
}

static void bb_destroy_instance(void *instance) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb) return;
    close_file(bb);
    free(bb);
    if (g_host && g_host->log) g_host->log("breakbeat: instance destroyed");
}

static void bb_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb || len < 1) return;
    
    if (msg[0] == 0xFA || msg[0] == 0xFB) {
        bb->clock_counter = 0;
        bb->bar_counter = 0;
        return;
    }
    
    // Handle MIDI Clock
    if (msg[0] == 0xF8) {
        bb->clock_counter++;
        if (bb->clock_counter % 96 == 0 && bb->clock_counter > 0) {
            bb->bar_counter++;
        }

        uint64_t current_samples = bb->sample_counter;
        uint64_t delta_samples = current_samples - bb->last_clock_samples;
        bb->last_clock_samples = current_samples;
        
        if (delta_samples > 100 && delta_samples < 10000) { // Sanity check
            float measured_bpm = (60.0f * 44100.0f) / ((float)delta_samples * 24.0f);
            // Smooth it to avoid jitter
            bb->calculated_bpm = bb->calculated_bpm * 0.95f + measured_bpm * 0.05f;
        }
        
        // Phrasing — swap to alt sample on last bar, restore main at top of next phrase.
        if (bb->phrase_bars > 0 && bb->clock_counter % 96 == 0) {
            int bar_in_phrase = (bb->clock_counter / 96) % bb->phrase_bars;

            if (bar_in_phrase == bb->phrase_bars - 1) {
                if ((float)rand() / (float)RAND_MAX < bb->swap_prob) {
                    snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path),
                             "%s", bb->alt_sample_path);
                }
            } else if (bar_in_phrase == 0) {
                snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path),
                         "%s", bb->main_sample_path);
            }
        }

        // Deferred sample loading at start of bar (every 96 clocks = 4 beats)
        if (bb->clock_counter % 96 == 0 && bb->pending_sample_path[0]) {
            apply_sample_path(bb, bb->pending_sample_path);
            bb->pending_sample_path[0] = '\0';
        }
        
        // Dynamic trigger interval based on length!
        int trigger_clocks = (int)(48.0f * bb->length);
        if (trigger_clocks < 6) trigger_clocks = 6; // Min 16th note resolution
        
        if (bb->clock_counter % trigger_clocks == 0) {
            int trigger_in_bar = (bb->clock_counter % 96) / trigger_clocks;
            int triggers_per_bar = 96 / trigger_clocks;
            if (triggers_per_bar < 1) triggers_per_bar = 1;
            int beat_position = (int)((float)trigger_in_bar * (8.0f / (float)triggers_per_bar)) & 7;

            slice_inputs_t in = {
                .current_slice = bb->current_slice,
                .beat_position = beat_position,
                .complexity    = bb->complexity,
                .anchor        = bb->anchor,
                .roll          = bb->roll,
                .phrase_bars   = bb->phrase_bars,
                .fill          = bb->fill,
                .bar_in_phrase = bb->phrase_bars > 0 ? bb->bar_counter % bb->phrase_bars : 0,
            };
            bb->current_slice = slice_select_next(&in, bb_rand, NULL);

            bb->sub_slice_counter = 0;
            if ((float)rand() / (float)RAND_MAX < bb->retrigger_prob) {
                bb->sub_slice_active = 1;
                
                // Randomize divisions if "Rand" is selected!
                if (bb->retrigger_rate_idx == 4) {
                    int r2 = rand() % 4;
                    if (r2 == 0) bb->retrigger_divisions = 2;
                    else if (r2 == 1) bb->retrigger_divisions = 3;
                    else if (r2 == 2) bb->retrigger_divisions = 4;
                    else if (r2 == 3) bb->retrigger_divisions = 8;
                }
            } else {
                bb->sub_slice_active = 0;
            }

            bb->play_pos = bb->slice_starts[bb->current_slice];
        }
        return;
    }
    
    if (len < 3) return;
    
    uint8_t status = msg[0] & 0xF0;
    uint8_t note = msg[1];
    uint8_t vel = msg[2];
    
    if (status == 0x90 && vel > 0) { // Note On
        // Map notes 36-43 (Pads 1-8 in bank?) to slices 0-7
        if (note >= 36 && note <= 43) {
            int slice = note - 36;
            bb->play_pos = bb->slice_starts[slice];
            bb->current_slice = slice;
            bb->playing = 1;
            if (g_host && g_host->log) {
                char buf[64];
                snprintf(buf, sizeof(buf), "breakbeat: triggered slice %d", slice);
                g_host->log(buf);
            }
        }
    }
}

static void bb_set_param(void *instance, const char *key, const char *val) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb || !key || !val) return;
    
    if (g_host && g_host->log) {
        char log_buf[256];
        snprintf(log_buf, sizeof(log_buf), "BB: set_param key=%s, val=%s", key, val);
        g_host->log(log_buf);
    }
    
    if (strcmp(key, "preset") == 0 || strcmp(key, "preset_index") == 0) {
        bb->preset_idx = atoi(val);
        if (bb->preset_idx < 0) bb->preset_idx = 0;
        if (bb->preset_idx >= g_num_presets) bb->preset_idx = g_num_presets - 1;

        preset_t *p = &g_presets[bb->preset_idx];
        bb->length = p->length;
        bb->complexity = p->complexity;
        bb->retrigger_prob = p->retrigger_prob;
        bb->retrigger_divisions = p->retrigger_divisions;
        bb->anchor = p->anchor;
        bb->roll = p->roll;
        bb->phrase_bars = p->phrase_bars;
        bb->fill = p->fill;

        resolve_sample_path(bb->module_dir, p->sample_path,     bb->main_sample_path, sizeof(bb->main_sample_path));
        resolve_sample_path(bb->module_dir, p->alt_sample_path, bb->alt_sample_path,  sizeof(bb->alt_sample_path));

        int is_running = 0;
        if (g_host && g_host->get_clock_status) {
            is_running = (g_host->get_clock_status() == 2);
        }

        if (!is_running) {
            apply_sample_path(bb, bb->main_sample_path);
            bb->pending_sample_path[0] = '\0';
        } else {
            snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path),
                     "%s", bb->main_sample_path);
        }
    }
    else if (strcmp(key, "sample_path") == 0 || strcmp(key, "loop") == 0) {
        /* New filepath browser sends sample_path; legacy state may send loop
         * as a numeric index or sample name. Translate either way to a path. */
        char raw[BB_PATH_MAX];
        raw[0] = '\0';
        if (strcmp(key, "loop") == 0) {
            int found = 0;
            for (int i = 0; i < 23; i++) {
                if (strcmp(g_loop_names[i], val) == 0) {
                    legacy_idx_to_path(i, raw, sizeof(raw));
                    found = 1;
                    break;
                }
            }
            if (!found && val[0] >= '0' && val[0] <= '9') {
                legacy_idx_to_path(atoi(val), raw, sizeof(raw));
            }
        } else {
            snprintf(raw, sizeof(raw), "%s", val);
        }
        if (!raw[0]) return;

        resolve_sample_path(bb->module_dir, raw, bb->main_sample_path, sizeof(bb->main_sample_path));

        int is_running = 0;
        if (g_host && g_host->get_clock_status) {
            is_running = (g_host->get_clock_status() == 2);
        }

        if (!is_running) {
            apply_sample_path(bb, bb->main_sample_path);
            bb->pending_sample_path[0] = '\0';
        } else {
            snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path),
                     "%s", bb->main_sample_path);
        }
    }
    else if (strcmp(key, "alt_sample_path") == 0 || strcmp(key, "alt_loop") == 0) {
        char raw[BB_PATH_MAX];
        raw[0] = '\0';
        if (strcmp(key, "alt_loop") == 0) {
            int found = 0;
            for (int i = 0; i < 23; i++) {
                if (strcmp(g_loop_names[i], val) == 0) {
                    legacy_idx_to_path(i, raw, sizeof(raw));
                    found = 1;
                    break;
                }
            }
            if (!found && val[0] >= '0' && val[0] <= '9') {
                legacy_idx_to_path(atoi(val), raw, sizeof(raw));
            }
        } else {
            snprintf(raw, sizeof(raw), "%s", val);
        }
        if (raw[0]) {
            resolve_sample_path(bb->module_dir, raw, bb->alt_sample_path, sizeof(bb->alt_sample_path));
        }
    }
    else if (strcmp(key, "length") == 0) {
        int idx = atoi(val);
        float lengths[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
        if (idx >= 0 && idx < 6) {
            bb->length = lengths[idx];
        }
    }
    else if (strcmp(key, "complexity") == 0) {
        bb->complexity = atof(val) / 100.0f;
        if (bb->complexity < 0.0f) bb->complexity = 0.0f;
        if (bb->complexity > 1.0f) bb->complexity = 1.0f;
    }
    else if (strcmp(key, "anchor") == 0) {
        bb->anchor = atof(val) / 100.0f;
        if (bb->anchor < 0.0f) bb->anchor = 0.0f;
        if (bb->anchor > 1.0f) bb->anchor = 1.0f;
    }
    else if (strcmp(key, "roll") == 0) {
        bb->roll = atof(val) / 100.0f;
        if (bb->roll < 0.0f) bb->roll = 0.0f;
        if (bb->roll > 1.0f) bb->roll = 1.0f;
    }
    else if (strcmp(key, "phrase") == 0) {
        int idx = -1;
        const char *phrase_names[] = {"Off", "2 bars", "4 bars", "8 bars", "16 bars"};
        for (int i = 0; i < 5; i++) {
            if (strcmp(phrase_names[i], val) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) idx = atoi(val);
        
        static const int phrase_values[] = {0, 2, 4, 8, 16};
        if (idx >= 0 && idx < 5) {
            bb->phrase_bars = phrase_values[idx];
        }
    }
    else if (strcmp(key, "fill") == 0) {
        bb->fill = atof(val) / 100.0f;
        if (bb->fill < 0.0f) bb->fill = 0.0f;
        if (bb->fill > 1.0f) bb->fill = 1.0f;
    }
    else if (strcmp(key, "retrigger") == 0) {
        bb->retrigger_prob = atof(val) / 100.0f;
        if (bb->retrigger_prob < 0.0f) bb->retrigger_prob = 0.0f;
        if (bb->retrigger_prob > 1.0f) bb->retrigger_prob = 1.0f;
    }
    else if (strcmp(key, "retrigger_rate") == 0) {
        int idx = atoi(val);
        if (idx < 0) idx = 0;
        if (idx > 4) idx = 4;
        bb->retrigger_rate_idx = idx;
        
        if (idx == 0) bb->retrigger_divisions = 2;
        else if (idx == 1) bb->retrigger_divisions = 3;
        else if (idx == 2) bb->retrigger_divisions = 4;
        else if (idx == 3) bb->retrigger_divisions = 8;
    }
    else if (strcmp(key, "swap_prob") == 0) {
        bb->swap_prob = atof(val) / 100.0f;
        if (bb->swap_prob < 0.0f) bb->swap_prob = 0.0f;
        if (bb->swap_prob > 1.0f) bb->swap_prob = 1.0f;
    }
    else if (strcmp(key, "save_preset") == 0) {
        int trigger = atoi(val);
        if (trigger == 1 && g_host && g_host->log) {
            char buf[1536];
            int len_idx = 2;
            if (bb->length == 0.25f) len_idx = 0;
            else if (bb->length == 0.5f) len_idx = 1;
            else if (bb->length == 1.0f) len_idx = 2;
            else if (bb->length == 2.0f) len_idx = 3;
            else if (bb->length == 4.0f) len_idx = 4;
            else if (bb->length == 8.0f) len_idx = 5;

            int phrase_idx = 0;
            if (bb->phrase_bars == 2) phrase_idx = 1;
            else if (bb->phrase_bars == 4) phrase_idx = 2;
            else if (bb->phrase_bars == 8) phrase_idx = 3;
            else if (bb->phrase_bars == 16) phrase_idx = 4;

            snprintf(buf, sizeof(buf), "PRESET_DATA: {\"name\":\"%s\",\"sample_path\":\"%s\",\"alt_sample_path\":\"%s\",\"length\":%d,\"complexity\":%d,\"anchor\":%d,\"roll\":%d,\"phrase\":%d,\"fill\":%d,\"retrigger\":%d,\"retrigger_rate\":%d}",
                g_presets[bb->preset_idx].name,
                bb->main_sample_path,
                bb->alt_sample_path,
                len_idx,
                (int)(bb->complexity * 100.0f),
                (int)(bb->anchor * 100.0f),
                (int)(bb->roll * 100.0f),
                phrase_idx,
                (int)(bb->fill * 100.0f),
                (int)(bb->retrigger_prob * 100.0f),
                bb->retrigger_divisions);
            g_host->log(buf);
        }
    }
    else if (strcmp(key, "state") == 0) {
        json_get_int(val, "preset_index", &bb->preset_idx);

        /* Prefer new sample_path; fall back to legacy `loop` index/name. */
        char path_str[BB_PATH_MAX];
        if (json_get_string(val, "sample_path", path_str, sizeof(path_str))) {
            resolve_sample_path(bb->module_dir, path_str, bb->main_sample_path, sizeof(bb->main_sample_path));
            snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path), "%s", bb->main_sample_path);
            char dbg[BB_PATH_MAX + 64];
            snprintf(dbg, sizeof(dbg), "breakbeat: state restore sample_path=%s", bb->main_sample_path);
            wp_log(dbg);
        } else {
            char loop_str[64];
            if (json_get_string(val, "loop", loop_str, sizeof(loop_str))) {
                int idx = -1;
                for (int i = 0; i < 23; i++) {
                    if (strcmp(g_loop_names[i], loop_str) == 0) { idx = i; break; }
                }
                if (idx < 0 && loop_str[0] >= '0' && loop_str[0] <= '9') {
                    idx = atoi(loop_str);
                }
                if (idx >= 0) {
                    char p[BB_PATH_MAX];
                    if (legacy_idx_to_path(idx, p, sizeof(p)) == 0) {
                        resolve_sample_path(bb->module_dir, p, bb->main_sample_path, sizeof(bb->main_sample_path));
                        snprintf(bb->pending_sample_path, sizeof(bb->pending_sample_path), "%s", bb->main_sample_path);
                    }
                }
            }
        }

        if (json_get_string(val, "alt_sample_path", path_str, sizeof(path_str))) {
            resolve_sample_path(bb->module_dir, path_str, bb->alt_sample_path, sizeof(bb->alt_sample_path));
        } else {
            char alt_str[64];
            if (json_get_string(val, "alt_loop", alt_str, sizeof(alt_str))) {
                int idx = -1;
                for (int i = 0; i < 23; i++) {
                    if (strcmp(g_loop_names[i], alt_str) == 0) { idx = i; break; }
                }
                if (idx < 0 && alt_str[0] >= '0' && alt_str[0] <= '9') {
                    idx = atoi(alt_str);
                }
                if (idx >= 0) {
                    char p[BB_PATH_MAX];
                    if (legacy_idx_to_path(idx, p, sizeof(p)) == 0) {
                        resolve_sample_path(bb->module_dir, p, bb->alt_sample_path, sizeof(bb->alt_sample_path));
                    }
                }
            }
        }

        /* Numeric fields are stored UNQUOTED in state JSON, so use
         * json_get_int (json_get_string would silently fail to find them). */
        int i;
        if (json_get_int(val, "length", &i)) {
            static const float lengths[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
            if (i >= 0 && i < 6) bb->length = lengths[i];
        }
        if (json_get_int(val, "complexity", &i)) {
            bb->complexity = (float)i / 100.0f;
            if (bb->complexity < 0.0f) bb->complexity = 0.0f;
            if (bb->complexity > 1.0f) bb->complexity = 1.0f;
        }
        if (json_get_int(val, "anchor", &i)) {
            bb->anchor = (float)i / 100.0f;
            if (bb->anchor < 0.0f) bb->anchor = 0.0f;
            if (bb->anchor > 1.0f) bb->anchor = 1.0f;
        }
        if (json_get_int(val, "roll", &i)) {
            bb->roll = (float)i / 100.0f;
            if (bb->roll < 0.0f) bb->roll = 0.0f;
            if (bb->roll > 1.0f) bb->roll = 1.0f;
        }
        if (json_get_int(val, "phrase", &i)) {
            static const int phrase_values[] = {0, 2, 4, 8, 16};
            if (i < 0) i = 0;
            if (i > 4) i = 4;
            bb->phrase_bars = phrase_values[i];
        }
        if (json_get_int(val, "fill", &i)) {
            bb->fill = (float)i / 100.0f;
            if (bb->fill < 0.0f) bb->fill = 0.0f;
            if (bb->fill > 1.0f) bb->fill = 1.0f;
        }
        if (json_get_int(val, "retrigger", &i)) {
            bb->retrigger_prob = (float)i / 100.0f;
            if (bb->retrigger_prob < 0.0f) bb->retrigger_prob = 0.0f;
            if (bb->retrigger_prob > 1.0f) bb->retrigger_prob = 1.0f;
        }
        if (json_get_int(val, "retrigger_rate", &i)) {
            if (i < 0) i = 0;
            if (i > 4) i = 4;
            bb->retrigger_rate_idx = i;
            if      (i == 0) bb->retrigger_divisions = 2;
            else if (i == 1) bb->retrigger_divisions = 3;
            else if (i == 2) bb->retrigger_divisions = 4;
            else if (i == 3) bb->retrigger_divisions = 8;
            /* i == 4 (Rand) leaves current divisions; randomized per stutter. */
        }
        if (json_get_int(val, "swap_prob", &i)) {
            bb->swap_prob = (float)i / 100.0f;
            if (bb->swap_prob < 0.0f) bb->swap_prob = 0.0f;
            if (bb->swap_prob > 1.0f) bb->swap_prob = 1.0f;
        }
    }
}

static int bb_get_param(void *instance, const char *key, char *buf, int buf_len) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb || !key || !buf || buf_len < 2) return -1;
    

    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{\"modes\":null,\"levels\":{\"root\":{\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\",\"knobs\":[\"preset\",\"sample_path\",\"alt_sample_path\",\"length\",\"phrase\",\"complexity\",\"anchor\",\"roll\",\"fill\",\"retrigger\",\"retrigger_rate\",\"swap_prob\",\"save_preset\"],\"params\":[{\"key\":\"preset\",\"label\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},{\"key\":\"sample_path\",\"label\":\"Sample\",\"type\":\"filepath\",\"root\":\"/data/UserData/breakbeat-samples\",\"filter\":\".wav\"},{\"key\":\"alt_sample_path\",\"label\":\"Alt Sample\",\"type\":\"filepath\",\"root\":\"/data/UserData/breakbeat-samples\",\"filter\":\".wav\"},{\"key\":\"length\",\"label\":\"Length\",\"type\":\"enum\",\"options\":[\"1/4 bar\",\"1/2 bar\",\"1 bar\",\"2 bars\",\"4 bars\",\"8 bars\"]},{\"key\":\"phrase\",\"label\":\"Phrase\",\"type\":\"enum\",\"options\":[\"Off\",\"2 bars\",\"4 bars\",\"8 bars\",\"16 bars\"]},{\"key\":\"complexity\",\"label\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"anchor\",\"label\":\"Anchor\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"roll\",\"label\":\"Roll\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"fill\",\"label\":\"Fill\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger\",\"label\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger_rate\",\"label\":\"Retrig Rate\",\"type\":\"enum\",\"options\":[\"2x\",\"3x\",\"4x\",\"8x\",\"Rand\"]},{\"key\":\"swap_prob\",\"label\":\"Swap Prob\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"save_preset\",\"label\":\"Save to Log\",\"type\":\"int\",\"min\":0,\"max\":1}]}}}";
        strncpy(buf, hierarchy, buf_len);
        return strlen(hierarchy);
    }
    if (strcmp(key, "chain_params") == 0) {
        const char *json = "["
            "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},"
            "{\"key\":\"sample_path\",\"name\":\"Sample\",\"type\":\"filepath\",\"root\":\"/data/UserData/breakbeat-samples\",\"filter\":\".wav\"},"
            "{\"key\":\"alt_sample_path\",\"name\":\"Alt Sample\",\"type\":\"filepath\",\"root\":\"/data/UserData/breakbeat-samples\",\"filter\":\".wav\"},"
            "{\"key\":\"length\",\"name\":\"Length\",\"type\":\"enum\",\"options\":[\"1/4 bar\",\"1/2 bar\",\"1 bar\",\"2 bars\",\"4 bars\",\"8 bars\"]},"
            "{\"key\":\"phrase\",\"name\":\"Phrase\",\"type\":\"enum\",\"options\":[\"Off\",\"2 bars\",\"4 bars\",\"8 bars\",\"16 bars\"]},"
            "{\"key\":\"complexity\",\"name\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"anchor\",\"name\":\"Anchor\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"roll\",\"name\":\"Roll\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"fill\",\"name\":\"Fill\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger\",\"name\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger_rate\",\"name\":\"Retrig Rate\",\"type\":\"enum\",\"options\":[\"2x\",\"3x\",\"4x\",\"8x\",\"Rand\"]},"
            "{\"key\":\"swap_prob\",\"name\":\"Swap Prob\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"save_preset\",\"name\":\"Save to Log\",\"type\":\"int\",\"min\":0,\"max\":1}"
        "]";
        strncpy(buf, json, buf_len);
        return strlen(json);
    }
    if (strcmp(key, "preset_count") == 0) {
        return snprintf(buf, buf_len, "%d", g_num_presets);
    }
    else if (strcmp(key, "preset") == 0 || strcmp(key, "preset_index") == 0) {
        return snprintf(buf, buf_len, "%d", bb->preset_idx);
    }
    else if (strcmp(key, "preset_name") == 0) {
        return snprintf(buf, buf_len, "%s", g_presets[bb->preset_idx].name);
    }
    else if (strcmp(key, "sample_path") == 0 || strcmp(key, "loop") == 0) {
        char dbg[BB_PATH_MAX + 64];
        snprintf(dbg, sizeof(dbg), "breakbeat: get_param sample_path -> %s", bb->main_sample_path);
        wp_log(dbg);
        return snprintf(buf, buf_len, "%s", bb->main_sample_path);
    }
    else if (strcmp(key, "length") == 0) {
        int idx = 2; // Default to 1.0
        if (bb->length == 0.25f) idx = 0;
        else if (bb->length == 0.5f) idx = 1;
        else if (bb->length == 1.0f) idx = 2;
        else if (bb->length == 2.0f) idx = 3;
        else if (bb->length == 4.0f) idx = 4;
        else if (bb->length == 8.0f) idx = 5;
        return snprintf(buf, buf_len, "%d", idx);
    }
    else if (strcmp(key, "complexity") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->complexity * 100.0f));
    }
    else if (strcmp(key, "anchor") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->anchor * 100.0f));
    }
    else if (strcmp(key, "roll") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->roll * 100.0f));
    }
    else if (strcmp(key, "phrase") == 0) {
        int idx = 0;
        if (bb->phrase_bars == 2) idx = 1;
        else if (bb->phrase_bars == 4) idx = 2;
        else if (bb->phrase_bars == 8) idx = 3;
        else if (bb->phrase_bars == 16) idx = 4;
        return snprintf(buf, buf_len, "%d", idx);
    }
    else if (strcmp(key, "alt_sample_path") == 0 || strcmp(key, "alt_loop") == 0) {
        return snprintf(buf, buf_len, "%s", bb->alt_sample_path);
    }
    else if (strcmp(key, "fill") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->fill * 100.0f));
    }
    else if (strcmp(key, "retrigger") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->retrigger_prob * 100.0f));
    }
    else if (strcmp(key, "retrigger_rate") == 0) {
        return snprintf(buf, buf_len, "%d", bb->retrigger_rate_idx);
    }
    else if (strcmp(key, "swap_prob") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->swap_prob * 100.0f));
    }
    else if (strcmp(key, "state") == 0) {
        int len_idx = 2; // Default to 1.0
        if (bb->length == 0.25f) len_idx = 0;
        else if (bb->length == 0.5f) len_idx = 1;
        else if (bb->length == 1.0f) len_idx = 2;
        else if (bb->length == 2.0f) len_idx = 3;
        else if (bb->length == 4.0f) len_idx = 4;
        else if (bb->length == 8.0f) len_idx = 5;

        int phrase_idx = 0;
        if (bb->phrase_bars == 2) phrase_idx = 1;
        else if (bb->phrase_bars == 4) phrase_idx = 2;
        else if (bb->phrase_bars == 8) phrase_idx = 3;
        else if (bb->phrase_bars == 16) phrase_idx = 4;

        return snprintf(buf, buf_len, "{\"preset\":%d,\"sample_path\":\"%s\",\"alt_sample_path\":\"%s\",\"length\":%d,\"complexity\":%d,\"anchor\":%d,\"roll\":%d,\"phrase\":%d,\"fill\":%d,\"retrigger\":%d,\"retrigger_rate\":%d,\"swap_prob\":%d}",
            bb->preset_idx,
            bb->main_sample_path,
            bb->alt_sample_path,
            len_idx,
            (int)(bb->complexity * 100.0f),
            (int)(bb->anchor * 100.0f),
            (int)(bb->roll * 100.0f),
            phrase_idx,
            (int)(bb->fill * 100.0f),
            (int)(bb->retrigger_prob * 100.0f),
            bb->retrigger_rate_idx,
            (int)(bb->swap_prob * 100.0f));
    }
    
    return -1;
}

static void bb_render_block(void *instance, int16_t *out_lr, int frames) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb || !bb->data || bb->total_frames == 0) {
        memset(out_lr, 0, frames * 2 * sizeof(int16_t));
        return;
    }
    
    // Auto-play on transport start, and STOP on stop!
    if (g_host && g_host->get_clock_status) {
        int running = (g_host->get_clock_status() == 2);
        if (running && !bb->playing) {
            srand((unsigned int)(time(NULL) ^ bb->sample_counter));
            bb->bar_counter = 0;
        }
        if (running) {
            bb->playing = 1;
        } else {
            bb->playing = 0;
            bb->play_pos = 0;
            bb->clock_counter = 0;
            bb->current_slice = 0;
            bb->bar_counter = 0;
        }
    }
    
    if (!bb->playing) {
        memset(out_lr, 0, frames * 2 * sizeof(int16_t));
        return;
    }
    
    const int nch = bb->num_channels;
    const int is_float = (bb->audio_format == WAV_FORMAT_FLOAT);
    const int bits = bb->bits_per_sample;

    float rate = 1.0f;
    float current_bpm = bb->calculated_bpm;
    float beats = 16.0f * bb->length; // Assume base length of 4 bars (16 beats)
    if (beats > 0.0f) {
        // Rate to fit total_frames into 'beats' at current_bpm
        rate = ((float)bb->total_frames * current_bpm) / (beats * 60.0f * 44100.0f);
    }
    
    for (int i = 0; i < frames; i++) {
        uint32_t idx = (uint32_t)bb->play_pos;
        if (idx >= bb->total_frames) {
            bb->play_pos = 0; // Loop always for now
            idx = 0;
        }
        
        float fL, fR;
        if (is_float) {
            /* 32-bit IEEE float */
            const float *fdata = (const float *)bb->data;
            if (nch == 1) {
                fL = fR = fdata[idx];
            } else {
                fL = fdata[idx * 2];
                fR = fdata[idx * 2 + 1];
            }
        } else if (bits == 16) {
            const int16_t *sdata = (const int16_t *)bb->data;
            if (nch == 1) {
                fL = fR = sdata[idx] / 32768.0f;
            } else {
                fL = sdata[idx * 2]     / 32768.0f;
                fR = sdata[idx * 2 + 1] / 32768.0f;
            }
        } else if (bits == 24) {
            /* 24-bit PCM, 3 bytes per sample, little-endian signed */
            const uint8_t *bdata = (const uint8_t *)bb->data;
            uint32_t base = idx * (uint32_t)nch * 3u;
            int32_t l = bdata[base] | (bdata[base + 1] << 8) | (bdata[base + 2] << 16);
            if (l & 0x800000) l |= (int32_t)0xFF000000;
            int32_t r = l;
            if (nch == 2) {
                r = bdata[base + 3] | (bdata[base + 4] << 8) | (bdata[base + 5] << 16);
                if (r & 0x800000) r |= (int32_t)0xFF000000;
            }
            fL = (float)l / 8388608.0f;
            fR = (float)r / 8388608.0f;
        } else if (bits == 32) {
            /* 32-bit signed PCM */
            const int32_t *sdata = (const int32_t *)bb->data;
            if (nch == 1) {
                fL = fR = (float)sdata[idx] / 2147483648.0f;
            } else {
                fL = (float)sdata[idx * 2]     / 2147483648.0f;
                fR = (float)sdata[idx * 2 + 1] / 2147483648.0f;
            }
        } else {
            fL = fR = 0.0f;
        }
        
        int32_t L = (int32_t)(fL * 32767.0f);
        int32_t R = (int32_t)(fR * 32767.0f);
        
        if (L > 32767) L = 32767; else if (L < -32768) L = -32768;
        if (R > 32767) R = 32767; else if (R < -32768) R = -32768;
        
        out_lr[i * 2]     = (int16_t)L;
        out_lr[i * 2 + 1] = (int16_t)R;
        
        if (bb->sub_slice_active) {
            uint32_t start = bb->slice_starts[bb->current_slice];
            uint32_t len = bb->slice_lengths[bb->current_slice];
            uint32_t part_len = len / bb->retrigger_divisions;
            
            if (bb->play_pos >= start + part_len) {
                if (bb->sub_slice_counter < bb->retrigger_divisions - 1) {
                    bb->play_pos = start; // Loop back!
                    bb->sub_slice_counter++;
                }
            }
        }
        
        bb->play_pos += rate;
    }
    bb->sample_counter += frames;
}

static plugin_api_v2_t g_plugin_api_v2 = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = bb_create_instance,
    .destroy_instance = bb_destroy_instance,
    .on_midi = bb_on_midi,
    .set_param = bb_set_param,
    .get_param = bb_get_param,
    .get_error = NULL,
    .render_block = bb_render_block
};

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    if (g_host && g_host->log) g_host->log("breakbeat: plugin initialized");
    return &g_plugin_api_v2;
}
