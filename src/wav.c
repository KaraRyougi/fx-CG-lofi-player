#include "wav.h"
#include <string.h>
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
        (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1]<<8;
}
const char *wav_parse_reader(wav_reader_t read, void *context,
    uint32_t size, wav_info_t *out)
{
    uint8_t header[16];
    if(size < 12) return "Not a RIFF/WAVE file";
    if(read(context,0,header,12)) return "WAV header read failed";
    if(memcmp(header,"RIFF",4) || memcmp(header+8,"WAVE",4))
        return "Not a RIFF/WAVE file";
    uint32_t riff_size = le32(header+4);
    if(riff_size < 4 || riff_size > size-8) return "Truncated RIFF";
    uint32_t end = riff_size+8, pos = 12;
    uint32_t offset = 0, count = 0, rate = 0, bits = 0;
    int have_fmt = 0, have_data = 0;
    while(pos < end) {
        if(end-pos < 8) return "Truncated chunk header";
        if(read(context,pos,header,8)) return "Chunk read failed";
        uint32_t n = le32(header+4);
        if(n > end-pos-8) return "Truncated WAV chunk";
        int fmt = !memcmp(header,"fmt ",4), data = !memcmp(header,"data",4);
        if(fmt) {
            if(have_fmt) return "Duplicate format chunk";
            if(n < 16) return "Use PCM 8/16-bit mono WAV";
            if(read(context,pos+8,header,16)) return "Format read failed";
            bits = le16(header+14);
            if(le16(header)!=1 || le16(header+2)!=1 || (bits!=8 && bits!=16))
                return "Use PCM 8/16-bit mono WAV";
            rate = le32(header+4);
            if(rate < 4000 || rate > 48000 || le16(header+12)!=bits/8 ||
                le32(header+8)!=rate*(bits/8))
                return "Invalid WAV sample rate";
            have_fmt = 1;
        }
        if(data) {
            if(have_data) return "Duplicate data chunk";
            offset = pos+8; count = n; have_data = 1;
        }
        pos += 8+n;
        if(n&1) {
            if(pos == end) return "Missing chunk padding";
            pos++;
        }
    }
    if(!have_fmt || !have_data || !count) return "Missing WAV audio";
    count /= bits/8;
    if(!count) return "Missing WAV audio";
    *out = (wav_info_t){ offset, count, rate, bits };
    return NULL;
}
static int memory_read(void *context, uint32_t offset, void *data, size_t size)
{
    memcpy(data,(const uint8_t *)context+offset,size);
    return 0;
}
const char *wav_parse(const uint8_t *data, size_t size, wav_t *out)
{
    if(size > UINT32_MAX) return "WAV too large";
    wav_info_t info;
    const char *error = wav_parse_reader(memory_read,(void *)data,(uint32_t)size,&info);
    if(!error) *out = (wav_t){ data+info.offset, info.count, info.rate, info.bits };
    return error;
}
void cgplayer_encode(uint8_t sample, uint8_t out[32])
{
    unsigned value = sample;
    for(unsigned i=0; i<32; i++) {
        unsigned bits = value > 7 ? 8 : value;
        out[i] = (uint8_t)((1u<<bits)-1u);
        value -= bits;
    }
}
