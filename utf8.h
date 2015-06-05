
enum {
    RuneError = 0xFFFD, // the unicode replacement character
    RuneMax = 0x10FFFF, // the maximum unicode code point
    RuneSelf = 0x80,
};

int decoderune(char *buf, size_t buflen, int32_t *r);
int decodelastrune(char *buf, size_t buflen, int32_t *r);
