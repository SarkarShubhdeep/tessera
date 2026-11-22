/* Text-to-speech helpers (review 10): splitting, WAV parsing, resampling. No ESP-IDF; tested on the Mac. */
#include "tts_parse.h"

#include <stdlib.h>
#include <string.h>

int tts_split(const char *text, char *buf, size_t buflen, const char **pieces, int max_pieces, size_t max)
{
    size_t len = strlen(text);
    if (len + 1 > buflen) {
        len = buflen - 1;
    }
    memcpy(buf, text, len);
    buf[len] = '\0';
    int n = 0;
    char *p = buf;
    while (*p && n < max_pieces) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        size_t rest = strlen(p);
        size_t cut = rest;
        if (rest > max) {
            cut = 0;
            for (size_t i = 0; i < max; i++) {   /* the last sentence end that fits */
                if ((p[i] == '.' || p[i] == '!' || p[i] == '?') && (p[i + 1] == ' ' || p[i + 1] == '\0')) {
                    cut = i + 1;
                }
            }
            if (cut == 0) {   /* no sentence end: the last space that fits */
                for (size_t i = max; i > 0; i--) {
                    if (p[i] == ' ') {
                        cut = i;
                        break;
                    }
                }
            }
            if (cut == 0) {
                cut = max;   /* one long word: hard cut */
            }
        }
        pieces[n++] = p;
        if (p[cut] == '\0') {
            break;
        }
        p[cut] = '\0';
        p += cut + 1;
    }
    return n;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool tts_wav_parse(const uint8_t *buf, size_t len, wav_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t i = 12;
    bool fmt = false;
    while (i + 8 <= len) {
        uint32_t sz = le32(buf + i + 4);
        const uint8_t *body = buf + i + 8;
        size_t avail = len - (i + 8);
        if (memcmp(buf + i, "fmt ", 4) == 0 && avail >= 16) {
            if (le16(body) != 1) {
                return false;   /* not plain PCM */
            }
            out->channels = le16(body + 2);
            out->rate = le32(body + 4);
            out->bits = le16(body + 14);
            fmt = true;
        } else if (memcmp(buf + i, "data", 4) == 0) {
            out->data = body;
            out->data_len = (sz == 0 || sz == 0xFFFFFFFFu || sz > avail) ? avail : sz;
            return fmt && out->bits == 16 && (out->channels == 1 || out->channels == 2) && out->rate >= 8000;
        }
        if (sz > avail) {
            return false;
        }
        i += 8 + sz + (sz & 1);
    }
    return false;
}

int16_t *tts_to_16k_mono(const wav_t *w, size_t *n_out, void *(*alloc)(size_t))
{
    size_t frames = w->data_len / (2u * w->channels);
    const uint8_t *d = w->data;
    /* Mono, with a 3-tap low-pass when going down in rate (enough for speech), into a float-free int path. */
    size_t n = (size_t)((uint64_t)frames * 16000 / w->rate);
    int16_t *out = alloc ? alloc(n * 2 + 2) : malloc(n * 2 + 2);
    if (!out) {
        return NULL;
    }
    bool down = w->rate > 16000;
    for (size_t j = 0; j < n; j++) {
        /* Position in the source, in 1/65536ths of a frame. */
        uint64_t pos = (uint64_t)j * w->rate * 65536 / 16000;
        size_t i0 = (size_t)(pos >> 16);
        int32_t frac = (int32_t)(pos & 0xffff);
        int32_t v[2];
        for (int k = 0; k < 2; k++) {
            size_t i = i0 + k < frames ? i0 + k : frames - 1;
            int32_t s = 0;
            int taps = 0;
            for (int t = down ? -1 : 0; t <= (down ? 1 : 0); t++) {
                size_t ii = (t < 0 && i == 0) ? 0 : (i + t >= frames ? frames - 1 : i + t);
                int32_t m = (int16_t)le16(d + ii * 2u * w->channels);
                if (w->channels == 2) {
                    m = (m + (int16_t)le16(d + ii * 4u + 2)) / 2;
                }
                s += m;
                taps++;
            }
            v[k] = s / taps;
        }
        out[j] = (int16_t)(v[0] + (((v[1] - v[0]) * frac) >> 16));
    }
    *n_out = n;
    return out;
}
