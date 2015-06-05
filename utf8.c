#include <stdint.h>
#include <stdlib.h>
#include "utf8.h"

// T0 0bbbbbbb
// T1 110bbbbb 10bbbbbb
// T2 1110bbbb 10bbbbbb 10bbbbbb
// T3 11110bbb 10bbbbbb 10bbbbbb 10bbbbbb

int _decoderune(unsigned char *buf, size_t buflen, int32_t *r) {
    size_t i, len;
    unsigned int x;

    if (buflen == 0) {
        *r = 0;
        return 0;
    }

    x = buf[0];
    if (x < 0x80) {
        *r = x;
        return 1;
    }
    if (x < 0xC0 || x >= 0xF8) {
        *r = RuneError;
        return 1;
    }
    if (x < 0xE0) {
        len = 2;
        *r = (x & 0x1F) << 6;
    } else if (x < 0xF0) {
        len = 3;
        *r = (x & 0x0F) << 12;
    } else if (x < 0xF8) {
        len = 4;
        *r = (x & 0x07) << 18;
    }

    if (buflen < len) {
        *r = RuneError;
        return len;
    }

    for (i = 1; i < len; i++) {
        if (buf[i] < 0x80 || buf[i] >= 0xC0) {
            *r = RuneError;
            return len;
        }
        *r |= buf[i] << (6*len-i);
    }
    return len;
}

int _decodelastrune(unsigned char *buf, size_t buflen, int32_t *r) {
    int len;
    size_t i, limit;
    if (buflen == 0) {
        *r = 0;
        return 0;
    }
    if (buf[buflen-1] < 0x80) {
        *r = buf[buflen-1];
        return 1;
    }
    limit = buflen - 4;
    if (limit > buflen) {
        limit = 0;
    }
    for (i = buflen - 2; i >= limit; i--) {
        if (buf[i] < 0x80 || buf[i] >= 0xC0) {
            break;
        }
    }
    len = _decoderune(buf+i, buflen-i, r);
    if (len != buflen-i) {
        *r = RuneError;
        return 1;
    }
    return len;
}

int decoderune(char *buf, size_t buflen, int32_t *r) {
    if (r == NULL) {
        int32_t rr;
        return _decoderune((unsigned char*)buf, buflen, &rr);
    }
    return _decoderune((unsigned char*)buf, buflen, r);
}

int decodelastrune(char *buf, size_t buflen, int32_t *r) {
    if (r == NULL) {
        int32_t rr;
        return _decodelastrune((unsigned char*)buf, buflen, &rr);
    }
    return _decodelastrune((unsigned char*)buf, buflen, r);
}
