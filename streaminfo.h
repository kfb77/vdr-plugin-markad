/*
 * recv.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __streaminfo_h_
#define __streaminfo_h_

#include <stdint.h>

#include "global.h"

class cMarkAdStreamInfo
{
private:
    // taken from femon
    enum
    {
        NAL_SEI     = 0x06, // Supplemental Enhancement Information
        NAL_SPS     = 0x07, // Sequence Parameter Set
        NAL_AUD     = 0x09, // Access Unit Delimiter
        NAL_END_SEQ = 0x0A  // End of Sequence
    };
    int nalUnescape(uint8_t *dst, const uint8_t *src, int len);

    void FindH264VideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
    void FindH262VideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
public:
    void FindVideoInfos(MarkAdContext *maContext, uchar *pkt, int len);
    void FindAC3AudioInfos(MarkAdContext *maContext, uchar *espkt, int eslen);


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