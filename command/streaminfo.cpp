/*
 * streaminfo.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "streaminfo.h"

cMarkAdStreamInfo::cMarkAdStreamInfo()
{
    Clear();
}

void cMarkAdStreamInfo::Clear()
{
    memset(&H264,0,sizeof(H264));
}

bool cMarkAdStreamInfo::FindAC3AudioInfos(MarkAdContext *maContext, uchar *espkt, int eslen)
{
#pragma pack(1)
    struct AC3HDR
    {
unsigned Sync1:
        8;
unsigned Sync2:
        8;
unsigned CrcH:
        8;
unsigned CrcL:
        8;
unsigned FrameSizeIndex:
        6;
unsigned SampleRateIndex:
        2;
unsigned BsMod:
        3;
unsigned BsID:
        5;
unsigned LFE_Mix_VarField:
        5;
unsigned AcMod:
        3;
    };
#pragma pack()
    if ((!maContext) || (!espkt)) return false;
    if (eslen<(int) sizeof(struct AC3HDR)) return false;

    struct AC3HDR *ac3hdr = (struct AC3HDR *) espkt;

    if ((ac3hdr->Sync1==0x0b) && (ac3hdr->Sync2==0x77))
    {
        // some extra checks
        if (ac3hdr->SampleRateIndex==3) return false; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return false; // reserved

        maContext->Audio.Info.Channels=0;
        unsigned int lfe_bitmask = 0x0;

        switch (ac3hdr->AcMod)
        {
        case 0:
            maContext->Audio.Info.Channels=2;
            lfe_bitmask=0x10;
            break;
        case 1:
            maContext->Audio.Info.Channels=1;
            lfe_bitmask=0x10;
            break;
        case 2:
            maContext->Audio.Info.Channels=2;
            lfe_bitmask=0x4;
            break;
        case 3:
            maContext->Audio.Info.Channels=3;
            lfe_bitmask=0x4;
            break;
        case 4:
            maContext->Audio.Info.Channels=3;
            lfe_bitmask=0x4;
            break;
        case 5:
            maContext->Audio.Info.Channels=4;
            lfe_bitmask=0x1;
            break;
        case 6:
            maContext->Audio.Info.Channels=4;
            lfe_bitmask=0x4;
            break;
        case 7:
            maContext->Audio.Info.Channels=5;
            lfe_bitmask=0x1;
            break;
        }

        if ((ac3hdr->LFE_Mix_VarField & lfe_bitmask)==lfe_bitmask)
            maContext->Audio.Info.Channels++;

        return true;
    }
    return false;
}

bool cMarkAdStreamInfo::FindVideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return false;
    if (!maContext->Info.VPid.Type) return false;

    switch (maContext->Info.VPid.Type)
    {
    case MARKAD_PIDTYPE_VIDEO_H264:
        return FindH264VideoInfos(maContext, pkt, len);
        break;
    case MARKAD_PIDTYPE_VIDEO_H262:
        return FindH262VideoInfos(maContext, pkt, len);
        break;
    }
    return false;
}

bool cMarkAdStreamInfo::FindH264VideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return false;

    int nalu=pkt[4] & 0x1F;

    maContext->Video.Info.Pict_Type=0;
    if (nalu==NAL_AUD)
    {
        if (pkt[5]==0x10)
        {
            maContext->Video.Info.Pict_Type=MA_I_TYPE;
            H264.use_field=false;
            return true;
        }
        else
        {
            /*
                  if (maContext->Video.Info.Interlaced)
                  {
                      if (H264.use_field) {
                          H264.use_field=false;
                          return true;
                      } else {
                          H264.use_field=true;
                      }
                  } else {
                      return true;
                  }
            */

            if (maContext->Video.Info.Interlaced) {
                if (H264.use_field) return true;
            } else {
                return true;
            }

        }
    }

    if ((nalu==NAL_SLICE) || (nalu==NAL_IDR_SLICE))
    {
        uint8_t *nal_data=(uint8_t*) alloca(len);
        if (!nal_data) return false;
        int nal_len = nalUnescape(nal_data, pkt + 5, len - 5);
        cBitStream bs(nal_data, nal_len);

        bs.skipUeGolomb(); // first_mb_in_slice
        bs.skipUeGolomb(); // slice_type
        bs.skipUeGolomb(); // pic_parameter_set_id
        if (H264.separate_colour_plane_flag)
        {
            bs.skipBits(2); // colour_plane_id
        }
        bs.skipBits(H264.log2_max_frame_num); // frame_num

        if (maContext->Video.Info.Interlaced)
        {
            if (bs.getBit()) // field_pic_flag
            {
                H264.use_field=bs.getBit(); // bottom_field_flag
            } else {
                H264.use_field=true;
            }
        }
    }

    if (nalu==NAL_SPS)
    {
        uint8_t *nal_data=(uint8_t*) alloca(len);
        if (!nal_data) return false;
        int nal_len = nalUnescape(nal_data, pkt + 5, len - 5);
        cBitStream bs(nal_data, nal_len);

        uint32_t width=0;
        uint32_t height=0;
        uint32_t aspect_ratio_idc=0;
        int sar_width=1,sar_height=1;

        int profile_idc = bs.getU8();                 // profile_idc
        bs.skipBits(8);                           // constraint_setN_flags and reserved_zero_Nbits
        bs.skipBits(8);                           // level_idc
        bs.skipUeGolomb();                        // seq_parameter_set_id

        int i,j;

        if ((profile_idc == 100) || (profile_idc == 110) || (profile_idc == 122) || (profile_idc == 244) ||
                (profile_idc==44) || (profile_idc==83) || (profile_idc==86))
        {
            if (bs.getUeGolomb() == 3)             // chroma_format_idc
                H264.separate_colour_plane_flag=bs.getBit();                      // separate_colour_plane_flag
            bs.skipUeGolomb();                     // bit_depth_luma_minus8
            bs.skipUeGolomb();                     // bit_depth_chroma_minus8
            bs.skipBit();                          // qpprime_y_zero_transform_bypass_flag
            if (bs.getBit())                       // seq_scaling_matrix_present_flag
            {
                for (i = 0; i < 8; ++i)
                {
                    if (bs.getBit())                // seq_scaling_list_present_flag[i]
                    {
                        int last = 8, next = 8, size = (i < 6) ? 16 : 64;
                        for (j = 0; j < size; ++j)
                        {
                            if (next)
                                next = (last + bs.getSeGolomb()) & 0xff;
                            last = next ? next : last;
                        }
                    }
                }
            }
        }
        H264.log2_max_frame_num=bs.getUeGolomb()+4; // log2_max_frame_num_minus4
        int pic_order_cnt_type = bs.getUeGolomb();     // pic_order_cnt_type
        if (pic_order_cnt_type == 0)
            bs.skipUeGolomb();                     // log2_max_pic_order_cnt_lsb_minus4
        else if (pic_order_cnt_type == 1)
        {
            bs.skipBit();                          // delta_pic_order_always_zero
            bs.skipSeGolomb();                     // offset_for_non_ref_pic
            bs.skipSeGolomb();                     // offset_for_top_to_bottom_field
            j = bs.getUeGolomb();                  // num_ref_frames_in_pic_order_cnt_cycle
            for (i = 0; i < j; ++i)
                bs.skipSeGolomb();                 // offset_for_ref_frame[i]
        }
        bs.skipUeGolomb();                        // max num_ref_frames
        bs.skipBit();                             // gaps_in_frame_num_value_allowed_flag
        width  = bs.getUeGolomb() + 1;            // pic_width_in_mbs_minus1
        height = bs.getUeGolomb() + 1;            // pic_height_in_mbs_minus1
        bool frame_mbs_only_flag = bs.getBit();        // frame_mbs_only_flag
        width  *= 16;
        height *= 16 * (frame_mbs_only_flag ? 1 : 2);
        if (!frame_mbs_only_flag)
            bs.skipBit();                         // mb_adaptive_frame_field_flag
        bs.skipBit();                             // direct_8x8_inference_flag
        if (bs.getBit())                          // frame_cropping_flag
        {
            uint32_t crop_left, crop_right, crop_top, crop_bottom;
            crop_left   = bs.getUeGolomb();        // frame_crop_left_offset
            crop_right  = bs.getUeGolomb();        // frame_crop_rigth_offset
            crop_top    = bs.getUeGolomb();        // frame_crop_top_offset
            crop_bottom = bs.getUeGolomb();        // frame_crop_bottom_offset
            width -= 2 * (crop_left + crop_right);
            if (frame_mbs_only_flag)
                height -= 2 * (crop_top + crop_bottom);
            else
                height -= 4 * (crop_top + crop_bottom);
        }
        // VUI parameters
        if (bs.getBit())                          // vui_parameters_present_flag
        {
            if (bs.getBit())                       // aspect_ratio_info_present
            {
                aspect_ratio_idc = bs.getU8();      // aspect_ratio_idc
                if (aspect_ratio_idc == 255)        // extended sar
                {
                    sar_width=bs.getBits(16);                 // sar_width
                    sar_height=bs.getBits(16);                 // sar_height
                }
            }
            if (bs.getBit())                       // overscan_info_present_flag
                bs.skipBit();                       // overscan_approriate_flag
            if (bs.getBit())                       // video_signal_type_present_flag
            {
                bs.skipBits(3);                     // video_format
                bs.skipBit();                       // video_full_range_flag
                if (bs.getBit())                    // colour_description_present_flag
                {
                    bs.skipBits(8);                  // colour_primaries
                    bs.skipBits(8);                  // transfer_characteristics
                    bs.skipBits(8);                  // matrix_coefficients
                }
            }
            if (bs.getBit())                       // chroma_loc_info_present_flag
            {
                bs.skipUeGolomb();                  // chroma_sample_loc_type_top_field
                bs.skipUeGolomb();                  // chroma_sample_loc_type_bottom_field
            }
            if (bs.getBit())                       // timing_info_present_flag
            {
                uint32_t num_units_in_tick, time_scale;
                num_units_in_tick = bs.getU32();    // num_units_in_tick
                time_scale        = bs.getU32();    // time_scale
                double frame_rate=0;
                if (num_units_in_tick > 0)
                {
                    frame_rate = time_scale / num_units_in_tick;
                    if (frame_mbs_only_flag) {
                        frame_rate/=2;
                    } else {
                        if (pic_order_cnt_type!=2) frame_rate/=2;
                    }
                }
                bool fixedframerate=false;
                fixedframerate=bs.getBit();                       // fixed_frame_rate_flag
                if ((fixedframerate==1) && (frame_rate!=0)) {
                    maContext->Video.Info.FramesPerSecond=frame_rate;
                }
            }
#if 0
            int nal_hrd_parameters_present_flag = bs.getBit(); // nal_hrd_parameters_present_flag
            if (nal_hrd_parameters_present_flag)
            {
                int cpb_cnt_minus1;
                cpb_cnt_minus1 = bs.getUeGolomb();  // cpb_cnt_minus1
                bs.skipBits(4);                     // bit_rate_scale
                bs.skipBits(4);                     // cpb_size_scale
                for (int i = 0; i <= cpb_cnt_minus1; i++)
                {
                    bs.skipUeGolomb();              // bit_rate_value_minus1[i]
                    bs.skipUeGolomb();              // cpb_size_value_minus1[i]
                    bs.skipBit();                   // cbr_flag[i]
                }
                bs.skipBits(5);                     // initial_cpb_removal_delay_length_minus1
                bs.skipBits(5);                     // cpb_removal_delay_length_minus1
                bs.skipBits(5);                     // dpb_output_delay_length_minus1
                bs.skipBits(5);                     // time_offset_length
            }
            int vlc_hrd_parameters_present_flag = bs.getBit(); // vlc_hrd_parameters_present_flag
            if (vlc_hrd_parameters_present_flag)
            {
                int cpb_cnt_minus1;
                cpb_cnt_minus1 = bs.getUeGolomb(); // cpb_cnt_minus1
                bs.skipBits(4);                    // bit_rate_scale
                bs.skipBits(4);                    // cpb_size_scale
                for (int i = 0; i <= cpb_cnt_minus1; i++)
                {
                    bs.skipUeGolomb();             // bit_rate_value_minus1[i]
                    bs.skipUeGolomb();             // cpb_size_value_minus1[i]
                    bs.skipBit();                  // cbr_flag[i]
                }
                bs.skipBits(5);                    // initial_cpb_removal_delay_length_minus1
                bs.skipBits(5);                    // cpb_removal_delay_length_minus1
                bs.skipBits(5);                    // dpb_output_delay_length_minus1
                bs.skipBits(5);                    // time_offset_length
            }
            cpb_dpb_delays_present_flag = (nal_hrd_parameters_present_flag | vlc_hrd_parameters_present_flag);
            if (cpb_dpb_delays_present_flag)
                bs.skipBit();                       // low_delay_hrd_flag
            bs.skipBit();                           // pic_struct_present_flag
            if (bs.getBit())                       // bitstream_restriction_flag
            {
                bs.skipBit();                       // motion_vectors_over_pic_boundaries_flag
                bs.skipUeGolomb();                  // max_bytes_per_pic_denom
                bs.skipUeGolomb();                  // max_bits_per_mb_denom
                bs.skipUeGolomb();                  // log2_max_mv_length_horizontal
                bs.skipUeGolomb();                  // log2_max_mv_length_vertical
                bs.skipUeGolomb();                  // num_reorder_frames
                bs.skipUeGolomb();                  // max_dec_frame_buffering
            }
#endif
        }

        if  ((bs.getIndex() / 8)>0)
        {
            // set values
            maContext->Video.Info.Interlaced=!frame_mbs_only_flag;
            maContext->Video.Info.Width=width;
            maContext->Video.Info.Height=height;

            switch (aspect_ratio_idc)
            {
            case 1:
                // square pixel
                maContext->Video.Info.AspectRatio.Num=1;
                maContext->Video.Info.AspectRatio.Den=1;

                if (height==1080)
                {
                    if (width==1920)
                    {
                        maContext->Video.Info.AspectRatio.Num=16;
                        maContext->Video.Info.AspectRatio.Den=9;
                    }
                }

                if (height==720)
                {
                    if (width==960)
                    {
                        maContext->Video.Info.AspectRatio.Num=4;
                        maContext->Video.Info.AspectRatio.Den=3;
                    }

                    if (width==1280)
                    {
                        maContext->Video.Info.AspectRatio.Num=16;
                        maContext->Video.Info.AspectRatio.Den=9;
                    }
                }
                break;
            case 2:
                maContext->Video.Info.AspectRatio.Num=12;
                maContext->Video.Info.AspectRatio.Den=31;
                break;
            case 3:
                maContext->Video.Info.AspectRatio.Num=10;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 4:
                maContext->Video.Info.AspectRatio.Num=16;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 5:
                maContext->Video.Info.AspectRatio.Num=40;
                maContext->Video.Info.AspectRatio.Den=33;
                break;
            case 6:
                maContext->Video.Info.AspectRatio.Num=24;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 7:
                maContext->Video.Info.AspectRatio.Num=20;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 8:
                maContext->Video.Info.AspectRatio.Num=32;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 9:
                maContext->Video.Info.AspectRatio.Num=80;
                maContext->Video.Info.AspectRatio.Den=33;
                break;
            case 10:
                maContext->Video.Info.AspectRatio.Num=18;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 11:
                maContext->Video.Info.AspectRatio.Num=15;
                maContext->Video.Info.AspectRatio.Den=11;
                break;
            case 12:
                maContext->Video.Info.AspectRatio.Num=64;
                maContext->Video.Info.AspectRatio.Den=33;
                break;
            case 13:
                maContext->Video.Info.AspectRatio.Num=160;
                maContext->Video.Info.AspectRatio.Den=99;
                break;
            case 14:
                maContext->Video.Info.AspectRatio.Num=4;
                maContext->Video.Info.AspectRatio.Den=3;
                break;
            case 15:
                maContext->Video.Info.AspectRatio.Num=3;
                maContext->Video.Info.AspectRatio.Den=2;
                break;
            case 16:
                maContext->Video.Info.AspectRatio.Num=2;
                maContext->Video.Info.AspectRatio.Den=1;
                break;
            case 255:
                // extended SAR
                maContext->Video.Info.AspectRatio.Num=sar_width;
                maContext->Video.Info.AspectRatio.Den=sar_height;
                break;
            }
        }
    }
    return false;
}

bool cMarkAdStreamInfo::FindH262VideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return false;

    struct H262_SequenceHdr
    {
unsigned Sync1:
        8;
unsigned Sync2:
        8;
unsigned Sync3:
        8;
unsigned Sync4:
        8;
unsigned WidthH:
        8;
unsigned HeightH:
        4;
unsigned WidthL:
        4;
unsigned HeightL:
        8;
unsigned FrameRateIndex:
        4;
unsigned AspectRatioIndex:
        4;
    };

    struct H262_PictureHdr
    {
unsigned Sync1:
        8;
unsigned Sync2:
        8;
unsigned Sync3:
        8;
unsigned Sync4:
        8;
unsigned TemporalReferenceH:
        8;
unsigned VBVDelay:
        3;
unsigned CodingType:
        3;
unsigned TemporalReferenceL:
        8;
    };

    struct H262_SequenceExt
    {
unsigned Sync1:
        8;
unsigned Sync2:
        8;
unsigned Sync3:
        8;
unsigned Sync4:
        8;
unsigned Profile:
        4;
unsigned StartCode:
        4;
unsigned WidthExtH:
        1;
unsigned Chroma:
        2;
unsigned Progressive:
        1;
unsigned Level:
        4;
unsigned BitRateExtH:
        5;
unsigned HightExt:
        2;
unsigned WidthExtL:
        1;
unsigned Marker:
        1;
unsigned BitRateExtL:
        7;
    };

    struct H262_SequenceExt *seqext = (struct H262_SequenceExt *) pkt;
    struct H262_SequenceHdr *seqhdr = (struct H262_SequenceHdr *) pkt;
    struct H262_PictureHdr *pichdr = (struct H262_PictureHdr *) pkt;

    if (pichdr->Sync1==0 && pichdr->Sync2==0 && pichdr->Sync3==1 && pichdr->Sync4==0)
    {
        if (maContext->Video.Info.Height==0) return false;

        switch (pichdr->CodingType)
        {
        case 1:
            maContext->Video.Info.Pict_Type=MA_I_TYPE;
            break;
        case 2:
            maContext->Video.Info.Pict_Type=MA_P_TYPE;
            break;
        case 3:
            maContext->Video.Info.Pict_Type=MA_B_TYPE;
            break;
        case 4:
            maContext->Video.Info.Pict_Type=MA_D_TYPE;
            break;
        default:
            return false;
            break;
        }
        return true;
    }

    if (seqext->Sync1==0 && seqext->Sync2==0 && seqext->Sync3==1 && seqext->Sync4==0xb5)
    {
        maContext->Video.Info.Interlaced=!seqext->Progressive;
    }

    if (seqhdr->Sync1==0 && seqhdr->Sync2==0 && seqhdr->Sync3==1 && seqhdr->Sync4==0xb3)
    {

        maContext->Video.Info.Height=(seqhdr->HeightH<<8)+seqhdr->HeightL;
        maContext->Video.Info.Width=(seqhdr->WidthH<<4)+seqhdr->WidthL;

        switch (seqhdr->AspectRatioIndex)
        {
        case 1:
            maContext->Video.Info.AspectRatio.Num=1;
            maContext->Video.Info.AspectRatio.Den=1;
            break;
        case 2:
            maContext->Video.Info.AspectRatio.Num=4;
            maContext->Video.Info.AspectRatio.Den=3;
            break;
        case 3:
            maContext->Video.Info.AspectRatio.Num=16;
            maContext->Video.Info.AspectRatio.Den=9;
            break;
        case 4:
            maContext->Video.Info.AspectRatio.Num=11; // actually 2.21:1
            maContext->Video.Info.AspectRatio.Den=5;
            break;
        default:
            break;
        }

        double fps=0;
        switch (seqhdr->FrameRateIndex)
        {
        case 1:
            fps=24000/1001; // 23.976 fps NTSC encapsulated
            break;
        case 2:
            fps=24.0; // Standard international cinema film rate
            break;
        case 3:
            fps=25.0; // PAL (625/50) video frame rate
            break;

        case 4:
            fps=30000/1001; // 29.97 NTSC video frame rate
            break;

        case 5:
            fps=30.0; // NTSC drop frame (525/60) video frame rate
            break;

        case 6:
            fps=50.0; // double frame rate/progressive PAL
            break;

        case 7:
            fps=60000/1001; // double frame rate NTSC
            break;

        case 8:
            fps=60.0; // double frame rate drop-frame NTSC
            break;

        default:
            break;
        }
        if (fps)
        {
            if (fps!=maContext->Video.Info.FramesPerSecond)
            {
                maContext->Video.Info.FramesPerSecond=fps;
            }
        }
    }
    return false;
}

// taken from femon
int cMarkAdStreamInfo::nalUnescape(uint8_t *dst, const uint8_t *src, int len)
{
    int s = 0, d = 0;

    while (s < len)
    {
        if (!src[s] && !src[s + 1])
        {
            // hit 00 00 xx
            dst[d] = dst[d + 1] = 0;
            s += 2;
            d += 2;
            if (src[s] == 3)
            {
                s++; // 00 00 03 xx --> 00 00 xx
                if (s >= len)
                    return d;
            }
        }
        dst[d++] = src[s++];
    }

    return d;
}


cBitStream::cBitStream(const uint8_t *buf, const int len)
        : data(buf),
        count(len*8),
        index(0)
{
}

cBitStream::~cBitStream()
{
}

int cBitStream::getBit()
{
    if (index >= count)
        return (1); // -> no infinite colomb's ...

    int r = (data[index >> 3] >> (7 - (index & 7))) & 1;
    ++index;

    return (r);
}

uint32_t cBitStream::getBits(uint32_t n)
{
    uint32_t r = 0;

    while (n--)
        r = (r | (getBit() << n));

    return (r);
}

void cBitStream::skipBits(uint32_t n)
{
    index += n;
}

uint32_t cBitStream::getUeGolomb()
{
    int n = 0;

    while (!getBit() && (n < 32))
        n++;

    return (n ? ((1 << n) - 1) + getBits(n) : 0);
}

int32_t cBitStream::getSeGolomb()
{
    uint32_t r = getUeGolomb() + 1;

    return ((r & 1) ? -(r >> 1) : (r >> 1));
}

void cBitStream::skipGolomb()
{
    int n = 0;

    while (!getBit() && (n < 32))
        n++;

    skipBits(n);
}

void cBitStream::skipUeGolomb()
{
    skipGolomb();
}

void cBitStream::skipSeGolomb()
{
    skipGolomb();
}

void cBitStream::byteAlign()
{
    int n = index % 8;

    if (n > 0)
        skipBits(8 - n);
}
