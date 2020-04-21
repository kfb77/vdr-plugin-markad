/*
 * streaminfo.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __streaminfo_h_
#define __streaminfo_h_

#include "global.h"

class cMarkAdStreamInfo
{
private:
    // taken from ffmpeg
    enum
    {
        NAL_SLICE     = 0x01, // Slice
        NAL_IDR_SLICE = 0x05, // IDR-Slice
        NAL_SEI       = 0x06, // Supplemental Enhancement Information
        NAL_SPS       = 0x07, // Sequence Parameter Set
        NAL_PPS	      = 0x08, // Picture Parameter Set
        NAL_AUD       = 0x09, // Access Unit Delimiter
        NAL_END_SEQ   = 0x0A, // End of Sequence
        NAL_FILLER    = 0x0C, // Filler data
        NAL_SPS_EXT   = 0x0D, // Sequence Parameter Set Extension
        NAL_AUX_SLICE = 0x19  // Auxilary Slice
    };

    struct H264
    {
      bool separate_colour_plane_flag;
      int log2_max_frame_num;
      bool use_field;
    } H264;

    int nalUnescape(uint8_t *dst, const uint8_t *src, int len);
    bool FindH264VideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
    bool FindH262VideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
public:
    cMarkAdStreamInfo();
    void Clear();
    bool FindVideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
    bool FindAC3AudioInfos(MarkAdContext *maContext, uchar *espkt, int eslen);
};

// taken from femon
class cBitStream
{
private:
    const uint8_t *data;
    int            count; // in bits
    int            index; // in bits

public:
    cBitStream(const uint8_t *buf, const int len);
    ~cBitStream();

    int            getBit();
    uint32_t       getBits(uint32_t n);
    void           skipBits(uint32_t n);
    uint32_t       getUeGolomb();
    int32_t        getSeGolomb();
    void           skipGolomb();
    void           skipUeGolomb();
    void           skipSeGolomb();
    void           byteAlign();

    void           skipBit()
    {
        skipBits(1);
    }
    uint32_t       getU8()
    {
        return getBits(8);
    }
    uint32_t       getU16()
    {
        return ((getBits(8) << 8) | getBits(8));
    }
    uint32_t       getU24()
    {
        return ((getBits(8) << 16) | (getBits(8) << 8) | getBits(8));
    }
    uint32_t       getU32()
    {
        return ((getBits(8) << 24) | (getBits(8) << 16) | (getBits(8) << 8) | getBits(8));
    }
    bool           isEOF()
    {
        return (index >= count);
    }
    void           reset()
    {
        index = 0;
    }
    int            getIndex()
    {
        return (isEOF() ? count : index);
    }
    const uint8_t *getData()
    {
        return (isEOF() ? NULL : data + (index / 8));
    }
};
#endif
