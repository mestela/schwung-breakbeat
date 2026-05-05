#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "plugin_api_v1.h"

/* WAV audio format codes */
#define WAV_FORMAT_PCM   1
#define WAV_FORMAT_FLOAT 3

typedef struct {
    int preset_idx;
    int loop_idx;
    float length;
    float complexity;
    
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
    int pending_loop_idx;
    float retrigger_prob;
    int sub_slice_active;
    int sub_slice_counter;
    int retrigger_divisions;
    
    uint64_t sample_counter;
    uint64_t last_clock_samples;
    float calculated_bpm;
    
} breakbeat_t;

static const host_api_v1_t *g_host = NULL;
static int g_num_presets = 3; // Stub

typedef struct {
    char name[64];
    int loop_idx;
    float length;
    float complexity;
    float retrigger_prob;
    int retrigger_divisions;
} preset_t;

static preset_t g_presets[] = {
    {"Calm", 0, 0.5f, 0.18f, 0.16f, 2},
    {"Mid", 1, 0.5f, 0.56f, 0.08f, 4},
    {"Frantic", 18, 0.25f, 0.80f, 0.11f, 4}
};

static const char *g_loop_names[] = {
    "amen01", "amen09", "amen18", "amen19", "amen20", "apache", "do", "eeloil", "fireeater", "funkydrummer", "groove", "hungup_0", "king", "kool", "mechanicalman", "neworleans", "riffin", "ripple", "sesame", "sport", "squib", "think", "useme"
};

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

static int open_wav(breakbeat_t *wp, const char *path) {
    close_file(wp);

    wp->fd = open(path, O_RDONLY);
    if (wp->fd < 0) {
        wp_log("breakbeat: failed to open file");
        return -1;
    }

    struct stat st;
    if (fstat(wp->fd, &st) < 0 || st.st_size < 44) {
        wp_log("breakbeat: file too small for WAV header");
        close_file(wp);
        return -1;
    }

    wp->map_size = (size_t)st.st_size;
    wp->map = mmap(NULL, wp->map_size, PROT_READ, MAP_PRIVATE, wp->fd, 0);
    if (wp->map == MAP_FAILED) {
        wp_log("breakbeat: mmap failed");
        wp->map = NULL;
        close_file(wp);
        return -1;
    }

    const uint8_t *raw = (const uint8_t *)wp->map;
    if (memcmp(raw, "RIFF", 4) != 0 || memcmp(raw + 8, "WAVE", 4) != 0) {
        wp_log("breakbeat: not a RIFF/WAVE file");
        close_file(wp);
        return -1;
    }

    uint32_t offset = 12;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    int found_fmt = 0;
    int found_data = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;

    while (offset + 8 <= wp->map_size) {
        const uint8_t *chunk = raw + offset;
        uint32_t chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format    = chunk[8]  | (chunk[9]  << 8);
            num_channels    = chunk[10] | (chunk[11] << 8);
            sample_rate     = chunk[12] | (chunk[13] << 8) | (chunk[14] << 16) | (chunk[15] << 24);
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
        close_file(wp);
        return -1;
    }

    int bytes_per_sample = 0;
    if (audio_format == WAV_FORMAT_PCM && bits_per_sample == 16) {
        bytes_per_sample = 2;
    } else if (audio_format == WAV_FORMAT_FLOAT && bits_per_sample == 32) {
        bytes_per_sample = 4;
    } else {
        wp_log("breakbeat: unsupported format");
        close_file(wp);
        return -1;
    }

    wp->audio_format = audio_format;
    wp->bits_per_sample = bits_per_sample;
    wp->num_channels = num_channels;
    wp->data = (void *)(raw + data_offset);
    wp->total_frames = data_size / (num_channels * bytes_per_sample);
    wp->play_pos = 0;

    // Calculate slices (divide by 8)
    uint32_t slice_size = wp->total_frames / 8;
    for (int i = 0; i < 8; i++) {
        wp->slice_starts[i] = i * slice_size;
        wp->slice_lengths[i] = slice_size;
    }

    char logbuf[128];
    snprintf(logbuf, sizeof(logbuf), "breakbeat: loaded %u frames, slices of %u", wp->total_frames, slice_size);
    wp_log(logbuf);

    return 0;
}

static void* bb_create_instance(const char *module_dir, const char *json_defaults) {
    breakbeat_t *bb = calloc(1, sizeof(breakbeat_t));
    if (!bb) return NULL;
    
    bb->preset_idx = 0;
    bb->loop_idx = 1; // Default to amen
    bb->pending_loop_idx = -1;
    bb->retrigger_prob = 0.0f;
    bb->sub_slice_active = 0;
    bb->sub_slice_counter = 0;
    bb->retrigger_divisions = 2;
    bb->length = 1.0f;
    bb->sample_counter = 0;
    bb->last_clock_samples = 0;
    bb->calculated_bpm = 150.0f; // Default fallback
    bb->complexity = 0.5f;
    bb->fd = -1;
    
    if (g_host && g_host->log) g_host->log("breakbeat: instance created");
    
    // Load default WAV to make sound immediately!
    open_wav(bb, "/data/UserData/schwung/modules/sound_generators/breakbeat/samples/amen.wav");
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
    
    // Handle MIDI Clock
    if (msg[0] == 0xF8) {
        bb->clock_counter++;
        
        uint64_t current_samples = bb->sample_counter;
        uint64_t delta_samples = current_samples - bb->last_clock_samples;
        bb->last_clock_samples = current_samples;
        
        if (delta_samples > 100 && delta_samples < 10000) { // Sanity check
            float measured_bpm = (60.0f * 44100.0f) / ((float)delta_samples * 24.0f);
            // Smooth it to avoid jitter
            bb->calculated_bpm = bb->calculated_bpm * 0.95f + measured_bpm * 0.05f;
        }
        
        // Deferred loop loading at start of bar (every 96 clocks = 4 beats)
        if (bb->clock_counter % 96 == 0 && bb->pending_loop_idx >= 0) {
            char path[512];
            snprintf(path, sizeof(path), "/data/UserData/schwung/modules/sound_generators/breakbeat/samples/%s.wav", g_loop_names[bb->pending_loop_idx]);
            open_wav(bb, path);
            bb->loop_idx = bb->pending_loop_idx;
            bb->pending_loop_idx = -1;
        }
        
        // Dynamic trigger interval based on length!
        int trigger_clocks = (int)(48.0f * bb->length);
        if (trigger_clocks < 6) trigger_clocks = 6; // Min 16th note resolution
        
        if (bb->clock_counter % trigger_clocks == 0) {
            float r = (float)rand() / (float)RAND_MAX;
            float comp = bb->complexity;
            
            // Bar sync: force slice 0 at start of 4-bar loop (384 clocks)
            if (bb->clock_counter % 384 == 0) {
                bb->current_slice = 0;
            }
            else if (r < comp) {
                // Pure randomness for complexity!
                bb->current_slice = rand() % 8;
            } else {
                // Sequential play
                bb->current_slice = (bb->current_slice + 1) % 8;
            }
            
            // Roll for retrigger (sub-slice)
            bb->sub_slice_counter = 0;
            if ((float)rand() / (float)RAND_MAX < bb->retrigger_prob) {
                bb->sub_slice_active = 1;
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
        
        // Apply preset values!
        preset_t *p = &g_presets[bb->preset_idx];
        bb->length = p->length;
        bb->complexity = p->complexity;
        bb->retrigger_prob = p->retrigger_prob;
        bb->retrigger_divisions = p->retrigger_divisions;
        
        int is_running = 0;
        if (g_host && g_host->get_clock_status) {
            is_running = (g_host->get_clock_status() == 2);
        }
        
        if (!is_running) {
            char path[512];
            snprintf(path, sizeof(path), "/data/UserData/schwung/modules/sound_generators/breakbeat/samples/%s.wav", g_loop_names[p->loop_idx]);
            open_wav(bb, path);
            bb->loop_idx = p->loop_idx;
            bb->pending_loop_idx = -1;
        } else {
            bb->pending_loop_idx = p->loop_idx; // Defer!
        }
    }
    else if (strcmp(key, "loop") == 0) {
        int idx = 0;
        int found = 0;
        for (int i = 0; i < 23; i++) {
            if (strcmp(g_loop_names[i], val) == 0) {
                idx = i;
                found = 1;
                break;
            }
        }
        if (!found) idx = atoi(val);

        int is_running = 0;
        if (g_host && g_host->get_clock_status) {
            is_running = (g_host->get_clock_status() == 2);
        }
        
        if (!is_running) {
            char path[512];
            snprintf(path, sizeof(path), "/data/UserData/schwung/modules/sound_generators/breakbeat/samples/%s.wav", g_loop_names[idx]);
            open_wav(bb, path);
            bb->loop_idx = idx;
            bb->pending_loop_idx = -1;
        } else {
            bb->pending_loop_idx = idx;
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
    else if (strcmp(key, "retrigger") == 0) {
        bb->retrigger_prob = atof(val) / 100.0f;
        if (bb->retrigger_prob < 0.0f) bb->retrigger_prob = 0.0f;
        if (bb->retrigger_prob > 1.0f) bb->retrigger_prob = 1.0f;
    }
    else if (strcmp(key, "retrigger_rate") == 0) {
        bb->retrigger_divisions = atoi(val);
        if (bb->retrigger_divisions < 2) bb->retrigger_divisions = 2;
        if (bb->retrigger_divisions > 4) bb->retrigger_divisions = 4;
    }
    else if (strcmp(key, "save_preset") == 0) {
        int trigger = atoi(val);
        if (trigger == 1 && g_host && g_host->log) {
            char buf[512];
            int len_idx = 2;
            if (bb->length == 0.25f) len_idx = 0;
            else if (bb->length == 0.5f) len_idx = 1;
            else if (bb->length == 1.0f) len_idx = 2;
            else if (bb->length == 2.0f) len_idx = 3;
            else if (bb->length == 4.0f) len_idx = 4;
            else if (bb->length == 8.0f) len_idx = 5;

            snprintf(buf, sizeof(buf), "PRESET_DATA: {\"name\":\"%s\",\"loop\":%d,\"length\":%d,\"complexity\":%d,\"retrigger\":%d,\"retrigger_rate\":%d}",
                g_presets[bb->preset_idx].name, bb->loop_idx, len_idx, (int)(bb->complexity * 100.0f), (int)(bb->retrigger_prob * 100.0f), bb->retrigger_divisions);
            g_host->log(buf);
        }
    }
    else if (strcmp(key, "state") == 0) {
        json_get_int(val, "preset_index", &bb->preset_idx);
        
        char loop_str[64];
        if (json_get_string(val, "loop", loop_str, sizeof(loop_str))) {
            int found = 0;
            for (int i = 0; i < 23; i++) {
                if (strcmp(g_loop_names[i], loop_str) == 0) {
                    bb->loop_idx = i;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (loop_str[0] >= '0' && loop_str[0] <= '9') {
                    bb->loop_idx = atoi(loop_str);
                }
            }
            
            // Set pending loop instead of loading immediately!
            bb->pending_loop_idx = bb->loop_idx;
        }
        
        char float_str[32];
        if (json_get_string(val, "length", float_str, sizeof(float_str))) {
            float len = atof(float_str);
            if (len < 0.375f) bb->length = 0.25f;
            else if (len < 0.75f) bb->length = 0.5f;
            else if (len < 1.5f) bb->length = 1.0f;
            else if (len < 3.0f) bb->length = 2.0f;
            else if (len < 6.0f) bb->length = 4.0f;
            else bb->length = 8.0f;
        }
        if (json_get_string(val, "complexity", float_str, sizeof(float_str))) {
            bb->complexity = atof(float_str);
        }
    }
}

static int bb_get_param(void *instance, const char *key, char *buf, int buf_len) {
    breakbeat_t *bb = (breakbeat_t *)instance;
    if (!bb || !key || !buf || buf_len < 2) return -1;
    

    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy = "{\"modes\":null,\"levels\":{\"root\":{\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\",\"knobs\":[\"preset\",\"loop\",\"length\",\"complexity\",\"retrigger\",\"retrigger_rate\",\"save_preset\"],\"params\":[{\"key\":\"preset\",\"label\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},{\"key\":\"loop\",\"label\":\"Loop\",\"type\":\"enum\",\"options\":[\"amen01\",\"amen09\",\"amen18\",\"amen19\",\"amen20\",\"apache\",\"do\",\"eeloil\",\"fireeater\",\"funkydrummer\",\"groove\",\"hungup_0\",\"king\",\"kool\",\"mechanicalman\",\"neworleans\",\"riffin\",\"ripple\",\"sesame\",\"sport\",\"squib\",\"think\",\"useme\"]},{\"key\":\"length\",\"label\":\"Length\",\"type\":\"enum\",\"options\":[\"0.25\",\"0.5\",\"1\",\"2\",\"4\",\"8\"]},{\"key\":\"complexity\",\"label\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger\",\"label\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},{\"key\":\"retrigger_rate\",\"label\":\"Retrig Rate\",\"type\":\"int\",\"min\":2,\"max\":4},{\"key\":\"save_preset\",\"label\":\"Save to Log\",\"type\":\"int\",\"min\":0,\"max\":1}]}}}";
        strncpy(buf, hierarchy, buf_len);
        return strlen(hierarchy);
    }
    if (strcmp(key, "chain_params") == 0) {
        const char *json = "["
            "{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":10},"
            "{\"key\":\"loop\",\"name\":\"Loop\",\"type\":\"enum\",\"options\":[\"amen01\",\"amen09\",\"amen18\",\"amen19\",\"amen20\",\"apache\",\"do\",\"eeloil\",\"fireeater\",\"funkydrummer\",\"groove\",\"hungup_0\",\"king\",\"kool\",\"mechanicalman\",\"neworleans\",\"riffin\",\"ripple\",\"sesame\",\"sport\",\"squib\",\"think\",\"useme\"]},"
            "{\"key\":\"length\",\"name\":\"Length\",\"type\":\"enum\",\"options\":[\"0.25\",\"0.5\",\"1\",\"2\",\"4\",\"8\"]},"
            "{\"key\":\"complexity\",\"name\":\"Complexity\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger\",\"name\":\"Retrigger\",\"type\":\"int\",\"min\":0,\"max\":100},"
            "{\"key\":\"retrigger_rate\",\"name\":\"Retrig Rate\",\"type\":\"int\",\"min\":2,\"max\":4},"
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
    else if (strcmp(key, "loop") == 0) {
        if (bb->loop_idx >= 0 && bb->loop_idx < 23) {
            return snprintf(buf, buf_len, "%s", g_loop_names[bb->loop_idx]);
        }
        return snprintf(buf, buf_len, "unknown");
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
    else if (strcmp(key, "retrigger") == 0) {
        return snprintf(buf, buf_len, "%d", (int)(bb->retrigger_prob * 100.0f));
    }
    else if (strcmp(key, "retrigger_rate") == 0) {
        return snprintf(buf, buf_len, "%d", bb->retrigger_divisions);
    }
    else if (strcmp(key, "state") == 0) {
        int len_idx = 2; // Default to 1.0
        if (bb->length == 0.25f) len_idx = 0;
        else if (bb->length == 0.5f) len_idx = 1;
        else if (bb->length == 1.0f) len_idx = 2;
        else if (bb->length == 2.0f) len_idx = 3;
        else if (bb->length == 4.0f) len_idx = 4;
        else if (bb->length == 8.0f) len_idx = 5;
        
        return snprintf(buf, buf_len, "{\"preset\":%d,\"loop\":%d,\"length\":%d,\"complexity\":%d,\"retrigger\":%d,\"retrigger_rate\":%d}",
            bb->preset_idx, bb->loop_idx, len_idx, (int)(bb->complexity * 100.0f), (int)(bb->retrigger_prob * 100.0f), bb->retrigger_divisions);
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
        if (g_host->get_clock_status() == 2) { // MOVE_CLOCK_STATUS_RUNNING
            bb->playing = 1;
        } else {
            bb->playing = 0;
            bb->play_pos = 0; // Reset to start
            bb->clock_counter = 0; // Reset clocks!
            bb->current_slice = 0; // Reset slice!
        }
    }
    
    if (!bb->playing) {
        memset(out_lr, 0, frames * 2 * sizeof(int16_t));
        return;
    }
    
    const int nch = bb->num_channels;
    const int is_float = (bb->audio_format == WAV_FORMAT_FLOAT);
    
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
            const float *fdata = (const float *)bb->data;
            if (nch == 1) {
                fL = fR = fdata[idx];
            } else {
                fL = fdata[idx * 2];
                fR = fdata[idx * 2 + 1];
            }
        } else {
            const int16_t *sdata = (const int16_t *)bb->data;
            if (nch == 1) {
                fL = fR = sdata[idx] / 32768.0f;
            } else {
                fL = sdata[idx * 2]     / 32768.0f;
                fR = sdata[idx * 2 + 1] / 32768.0f;
            }
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
