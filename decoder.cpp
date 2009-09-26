/*
 * decoder.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "decoder.h"

void cMarkAdDecoder::FindAC3AudioInfos(MarkAdContext *maContext, uchar *espkt, int eslen)
{
    if ((!maContext) || (!espkt)) return;

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

    struct AC3HDR *ac3hdr = (struct AC3HDR *) espkt;

    if ((ac3hdr->Sync1==0x0b) && (ac3hdr->Sync2==0x77))
    {
        // some extra checks
        if (ac3hdr->SampleRateIndex==3) return; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return; // reserved

        maContext->Audio.Info.Channels=0;
        int lfe_bitmask = 0x0;

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
    }

}

void cMarkAdDecoder::FindVideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return;

    if (maContext->General.H264)
    {
        FindH264VideoInfos(maContext, pkt, len);
    }
    else
    {
        FindH262VideoInfos(maContext, pkt, len);
    }
}

void cMarkAdDecoder::FindH264VideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return;

    if ((pkt[3] & 0x1F)==NAL_AUD)
    {
        switch (pkt[4] >> 5)
        {
        case 0:
        case 3:
        case 5: // I_FRAME
            maContext->Video.Info.Pict_Type=MA_I_TYPE;
            break;
        case 1:
        case 4:
        case 6: // P_FRAME;
            maContext->Video.Info.Pict_Type=MA_P_TYPE;
            break;
        case 2:
        case 7:         // B_FRAME;
            maContext->Video.Info.Pict_Type=MA_B_TYPE;
            break;
        default:                // NO_PICTURE;
            maContext->Video.Info.Pict_Type=0;
            break;
        }
    }

    if ((pkt[3] & 0x1F)==NAL_SPS)
    {
        uint8_t nal_data[len];
        const uint8_t *end = pkt + len;
        int nal_len = nalUnescape(nal_data, pkt + 4, int(end - pkt - 4));

        int profile_idc, level_idc, constraint_set3_flag, pic_order_cnt_type, i, j;
        cBitStream bs(nal_data, nal_len);

        uint32_t width=0;
        uint32_t height=0;
        uint32_t aspect_ratio_idc=0;
        uint32_t video_format=0;
        double frame_rate=0;
        double bit_rate=0;
        bool cpb_dpb_delays_present_flag=false;
        bool pic_struct_present_flag=false;
        bool frame_mbs_only_flag=false;
        bool mb_adaptive_frame_field_flag=false;
        uint32_t time_offset_length=0;

        profile_idc = bs.getU8();                 // profile_idc
        bs.skipBit();                             // constraint_set0_flag
        bs.skipBit();                             // constraint_set1_flag
        bs.skipBit();                             // constraint_set2_flag
        constraint_set3_flag = bs.getBit();       // constraint_set3_flag
        bs.skipBits(4);                           // reserved_zero_4bits
        level_idc = bs.getU8();                   // level_idc
        bs.skipUeGolomb();                        // seq_parameter_set_id

        switch (profile_idc)
        {
        case 66:                                // baseline profile
        case 77:                                // main profile
        case 88:                                // extended profile
            switch (level_idc)
            {
            case 10:                         // level 1.0
                bit_rate = 64000;
                break;
            case 11:                         // level 1b / 1.1
                bit_rate = constraint_set3_flag ? 128000 : 192000;
                break;
            case 12:                         // level 1.2
                bit_rate = 384000;
                break;
            case 13:                         // level 1.3
                bit_rate = 768000;
                break;
            case 20:                         // level 2.0
                bit_rate = 2000000;
                break;
            case 21:                         // level 2.1
                bit_rate = 4000000;
                break;
            case 22:                         // level 2.2
                bit_rate = 4000000;
                break;
            case 30:                         // level 3.0
                bit_rate = 10000000;
                break;
            case 31:                         // level 3.1
                bit_rate = 14000000;
                break;
            case 32:                         // level 3.2
                bit_rate = 20000000;
                break;
            case 40:                         // level 4.0
                bit_rate = 20000000;
                break;
            case 41:                         // level 4.1
                bit_rate = 50000000;
                break;
            case 42:                         // level 4.2
                bit_rate = 50000000;
                break;
            case 50:                         // level 5.0
                bit_rate = 135000000;
                break;
            case 51:                         // level 5.1
                bit_rate = 240000000;
                break;
            default:
                break;
            }
            break;
        case 100:                               // high profile
            switch (level_idc)
            {
            case 10:                         // level 1.0
                bit_rate = 80000;
                break;
            case 11:                         // level 1b / 1.1
                bit_rate = constraint_set3_flag ? 160000 : 240000;
                break;
            case 12:                         // level 1.2
                bit_rate = 480000;
                break;
            case 13:                         // level 1.3
                bit_rate = 960000;
                break;
            case 20:                         // level 2.0
                bit_rate = 2500000;
                break;
            case 21:                         // level 2.1
                bit_rate = 5000000;
                break;
            case 22:                         // level 2.2
                bit_rate = 5000000;
                break;
            case 30:                         // level 3.0
                bit_rate = 12500000;
                break;
            case 31:                         // level 3.1
                bit_rate = 17500000;
                break;
            case 32:                         // level 3.2
                bit_rate = 25000000;
                break;
            case 40:                         // level 4.0
                bit_rate = 25000000;
                break;
            case 41:                         // level 4.1
                bit_rate = 62500000;
                break;
            case 42:                         // level 4.2
                bit_rate = 62500000;
                break;
            case 50:                         // level 5.0
                bit_rate = 168750000;
                break;
            case 51:                         // level 5.1
                bit_rate = 300000000;
                break;
            default:
                break;
            }
            break;
        case 110:                               // high 10 profile
            switch (level_idc)
            {
            case 10:                         // level 1.0
                bit_rate = 192000;
                break;
            case 11:                         // level 1b / 1.1
                bit_rate = constraint_set3_flag ? 384000 : 576000;
                break;
            case 12:                         // level 1.2
                bit_rate = 115200;
                break;
            case 13:                         // level 1.3
                bit_rate = 2304000;
                break;
            case 20:                         // level 2.0
                bit_rate = 6000000;
                break;
            case 21:                         // level 2.1
                bit_rate = 12000000;
                break;
            case 22:                         // level 2.2
                bit_rate = 12000000;
                break;
            case 30:                         // level 3.0
                bit_rate = 30000000;
                break;
            case 31:                         // level 3.1
                bit_rate = 42000000;
                break;
            case 32:                         // level 3.2
                bit_rate = 60000000;
                break;
            case 40:                         // level 4.0
                bit_rate = 60000000;
                break;
            case 41:                         // level 4.1
                bit_rate = 150000000;
                break;
            case 42:                         // level 4.2
                bit_rate = 150000000;
                break;
            case 50:                         // level 5.0
                bit_rate = 405000000;
                break;
            case 51:                         // level 5.1
                bit_rate = 720000000;
                break;
            default:
                break;
            }
            break;
        case 122:                               // high 4:2:2 profile
        case 144:                               // high 4:4:4 profile
            switch (level_idc)
            {
            case 10:                         // level 1.0
                bit_rate = 256000;
                break;
            case 11:                         // level 1b / 1.1
                bit_rate = constraint_set3_flag ? 512000 : 768000;
                break;
            case 12:                         // level 1.2
                bit_rate = 1536000;
                break;
            case 13:                         // level 1.3
                bit_rate = 3072000;
                break;
            case 20:                         // level 2.0
                bit_rate = 8000000;
                break;
            case 21:                         // level 2.1
                bit_rate = 16000000;
                break;
            case 22:                         // level 2.2
                bit_rate = 16000000;
                break;
            case 30:                         // level 3.0
                bit_rate = 40000000;
                break;
            case 31:                         // level 3.1
                bit_rate = 56000000;
                break;
            case 32:                         // level 3.2
                bit_rate = 80000000;
                break;
            case 40:                         // level 4.0
                bit_rate = 80000000;
                break;
            case 41:                         // level 4.1
                bit_rate = 200000000;
                break;
            case 42:                         // level 4.2
                bit_rate = 200000000;
                break;
            case 50:                         // level 5.0
                bit_rate = 540000000;
                break;
            case 51:                         // level 5.1
                bit_rate = 960000000;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
        if ((profile_idc == 100) || (profile_idc == 110) || (profile_idc == 122) || (profile_idc == 144))
        {
            if (bs.getUeGolomb() == 3)             // chroma_format_idc
                bs.skipBit();                       // residual_colour_transform_flag
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
                            last = next ?: last;
                        }
                    }
                }
            }
        }
        bs.skipUeGolomb();                        // log2_max_frame_num_minus4
        pic_order_cnt_type = bs.getUeGolomb();    // pic_order_cnt_type
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
        bs.skipUeGolomb();                        // num_ref_frames
        bs.skipBit();                             // gaps_in_frame_num_value_allowed_flag
        width  = bs.getUeGolomb() + 1;            // pic_width_in_mbs_minus1
        height = bs.getUeGolomb() + 1;            // pic_height_in_mbs_minus1
        frame_mbs_only_flag = bs.getBit();        // frame_mbs_only_flag
        width  *= 16;
        height *= 16 * (frame_mbs_only_flag ? 1 : 2);
        if (!frame_mbs_only_flag)
            mb_adaptive_frame_field_flag = bs.getBit(); // mb_adaptive_frame_field_flag
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
                    bs.skipBits(16);                 // sar_width
                    bs.skipBits(16);                 // sar_height
                }
            }
            if (bs.getBit())                       // overscan_info_present_flag
                bs.skipBit();                       // overscan_approriate_flag
            if (bs.getBit())                       // video_signal_type_present_flag
            {
                video_format = bs.getBits(3);       // video_format
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
                if (num_units_in_tick > 0)
                    frame_rate = time_scale / num_units_in_tick;
                bs.skipBit();                       // fixed_frame_rate_flag
            }
            int nal_hrd_parameters_present_flag = bs.getBit(); // nal_hrd_parameters_present_flag
            if (nal_hrd_parameters_present_flag)
            {
                int cpb_cnt_minus1;
                cpb_cnt_minus1 = bs.getUeGolomb();  // cpb_cnt_minus1
                bs.skipBits(4);                     // bit_rate_scale
                bs.skipBits(4);                     // cpb_size_scale
                for (int i = 0; i < cpb_cnt_minus1; ++i)
                {
                    bs.skipUeGolomb();              // bit_rate_value_minus1[i]
                    bs.skipUeGolomb();              // cpb_size_value_minus1[i]
                    bs.skipBit();                   // cbr_flag[i]
                }
                bs.skipBits(5);                     // initial_cpb_removal_delay_length_minus1
                bs.skipBits(5);                     // cpb_removal_delay_length_minus1
                bs.skipBits(5);                     // dpb_output_delay_length_minus1
                time_offset_length = bs.getBits(5); // time_offset_length
            }
            int vlc_hrd_parameters_present_flag = bs.getBit(); // vlc_hrd_parameters_present_flag
            if (vlc_hrd_parameters_present_flag)
            {
                int cpb_cnt_minus1;
                cpb_cnt_minus1 = bs.getUeGolomb(); // cpb_cnt_minus1
                bs.skipBits(4);                    // bit_rate_scale
                bs.skipBits(4);                    // cpb_size_scale
                for (int i = 0; i < cpb_cnt_minus1; ++i)
                {
                    bs.skipUeGolomb();             // bit_rate_value_minus1[i]
                    bs.skipUeGolomb();             // cpb_size_value_minus1[i]
                    bs.skipBit();                  // cbr_flag[i]
                }
                bs.skipBits(5);                    // initial_cpb_removal_delay_length_minus1
                bs.skipBits(5);                    // cpb_removal_delay_length_minus1
                bs.skipBits(5);                    // dpb_output_delay_length_minus1
                time_offset_length = bs.getBits(5);// time_offset_length
            }
            cpb_dpb_delays_present_flag = (nal_hrd_parameters_present_flag | vlc_hrd_parameters_present_flag);
            if (cpb_dpb_delays_present_flag)
                bs.skipBit();                       // low_delay_hrd_flag
            pic_struct_present_flag = bs.getBit(); // pic_struct_present_flag
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
        }

        if  ((bs.getIndex() / 8)>0)
        {
            // set values
            maContext->Video.Info.Width=width;
            maContext->Video.Info.Height=height;

            switch (aspect_ratio_idc)
            {
            case 1:
                maContext->Video.Info.AspectRatio.Num=1;
                maContext->Video.Info.AspectRatio.Den=1;
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
            }
        }
    }
}

void cMarkAdDecoder::FindH262VideoInfos(MarkAdContext *maContext, uchar *pkt, int len)
{
    if ((!maContext) || (!pkt) || (!len)) return;

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

    struct H262_SequenceHdr *seqhdr = (struct H262_SequenceHdr *) pkt;
    struct H262_PictureHdr *pichdr = (struct H262_PictureHdr *) pkt;

    if (pichdr->Sync1==0 && pichdr->Sync2==0 && pichdr->Sync3==1 && pichdr->Sync4==0)
    {
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
            maContext->Video.Info.Pict_Type=0;
            break;
        }
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
    }

}

cMarkAdDecoder::cMarkAdDecoder(int RecvNumber, bool useH264, bool hasAC3)
{
    recvnumber=RecvNumber;
#ifdef HAVE_AVCODEC
    avcodec_init();
    avcodec_register_all();

    cpu_set_t cpumask;
    uint len = sizeof(cpumask);
    int cpucount;
    if (sched_getaffinity(0,len,&cpumask)<0)
    {
        cpucount=1;
    }
    else
    {
        cpucount=CPU_COUNT(&cpumask);
    }

    isyslog("markad [%i]: using %i threads",recvnumber,cpucount);

    CodecID mp2_codecid=CODEC_ID_MP2;
    AVCodec *mp2_codec= avcodec_find_decoder(mp2_codecid);
    if (mp2_codec)
    {
        mp2_context = avcodec_alloc_context();
        if (mp2_context)
        {
            mp2_context->thread_count=cpucount;
            if (avcodec_open(mp2_context, mp2_codec) < 0)
            {
                esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,mp2_codecid);
                av_free(mp2_context);
                mp2_context=NULL;
            }
        }
        else
        {
            esyslog("markad [%i]: could not allocate mp2 context",recvnumber);
        }
    }
    else
    {
        esyslog("markad [%i]: codec 0x%05x not found",recvnumber,mp2_codecid);
        mp2_context=NULL;
    }

    if (hasAC3)
    {
        CodecID ac3_codecid=CODEC_ID_AC3;
        AVCodec *ac3_codec= avcodec_find_decoder(ac3_codecid);
        if (ac3_codec)
        {
            ac3_context = avcodec_alloc_context();
            if (ac3_context)
            {
                ac3_context->thread_count=cpucount;
                if (avcodec_open(ac3_context, ac3_codec) < 0)
                {
                    esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,ac3_codecid);
                    av_free(ac3_context);
                    ac3_context=NULL;
                }
            }
            else
            {
                esyslog("markad [%i]: could not allocate ac3 context",recvnumber);
            }
        }
        else
        {
            esyslog("markad [%i]: codec 0x%05x not found",recvnumber,ac3_codecid);
            ac3_context=NULL;
        }
    }
    else
    {
        ac3_context=NULL;
    }

    AVCodec *video_codec=NULL;
    CodecID video_codecid;

    if (useH264)
    {
        video_codecid=CODEC_ID_H264;
    }
    else
    {
        video_codecid=CODEC_ID_MPEG2VIDEO;
    }

    video_codec = avcodec_find_decoder(video_codecid);
    if (video_codec)
    {
        video_context = avcodec_alloc_context();
        if (video_context)
        {
            video_context->thread_count=cpucount;
            if (video_codec->capabilities & CODEC_CAP_TRUNCATED)
                video_context->flags|=CODEC_FLAG_TRUNCATED; // we do not send complete frames
            video_context->flags|=CODEC_FLAG_EMU_EDGE; // now linesize should be the same as width
            video_context->flags2|=CODEC_FLAG2_CHUNKS; // needed for H264!
            video_context->flags2|=CODEC_FLAG2_FAST; // really?

            if (avcodec_open(video_context, video_codec) < 0)
            {
                esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,video_codecid);
                av_free(video_context);
                video_context=NULL;
            }
            else
            {
                video_frame = avcodec_alloc_frame();
                if (!video_frame)
                {
                    esyslog("markad [%i]: could not allocate frame",recvnumber);
                    avcodec_close(video_context);
                    av_free(video_context);
                    video_context=NULL;
                }
            }
        }
        else
        {
            esyslog("markad [%i]: could not allocate video context",recvnumber);
        }
    }
    else
    {
        esyslog("markad [%i]: codec 0x%05x not found",recvnumber,video_codecid);
        video_context=NULL;
    }
    memset(temp_pictureplane,0,sizeof(temp_pictureplane));
#endif
}

cMarkAdDecoder::~cMarkAdDecoder()
{
#ifdef HAVE_AVCODEC
    if (video_context)
    {
        avcodec_close(video_context);
        av_free(video_context);
        av_free(video_frame);
    }

    if (ac3_context)
    {
        avcodec_close(ac3_context);
        av_free(ac3_context);
    }

    if (mp2_context)
    {
        avcodec_close(mp2_context);
        av_free(mp2_context);
    }
    SetVideoInfos(NULL,NULL,NULL,NULL);
#endif
}

bool cMarkAdDecoder::DecodeMP2(MarkAdContext *maContext, uchar *espkt, int eslen)
{
#ifdef HAVE_AVCODEC
    if (!mp2_context) return false;
    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=espkt;
    avpkt.size=eslen;

    DECLARE_ALIGNED(16,char,outbuf[AVCODEC_MAX_AUDIO_FRAME_SIZE]);
    int outbuf_size=AVCODEC_MAX_AUDIO_FRAME_SIZE;
    int ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(mp2_context,(short *) &outbuf,&outbuf_size,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(mp2_context,(short *) &outbuf,&outbuf_size,&avpkt);
#endif
        if (len<0)
        {
            esyslog("markad [%i]: error decoding mp2",recvnumber);
            break;
        }
        else
        {
            SetAudioInfos(maContext,ac3_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
    }
    return ret;
#else
    return true;
#endif
}

#ifdef HAVE_AVCODEC
bool cMarkAdDecoder::SetAudioInfos(MarkAdContext *maContext, AVCodecContext *Audio_Context)
{
    if ((!maContext) || (!Audio_Context)) return false;

    maContext->Audio.Info.Channels = Audio_Context->channels;
    return true;
}
#endif

bool cMarkAdDecoder::DecodeAC3(MarkAdContext *maContext, uchar *espkt, int eslen)
{
#ifdef HAVE_AVCODEC
    if (!ac3_context) return false;
    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=espkt;
    avpkt.size=eslen;

    DECLARE_ALIGNED(16,char,outbuf[AVCODEC_MAX_AUDIO_FRAME_SIZE]);
    int outbuf_size=AVCODEC_MAX_AUDIO_FRAME_SIZE;
    int ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(ac3_context,(short *) &outbuf,&outbuf_size,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(ac3_context,(short *) &outbuf,&outbuf_size,&avpkt);
#endif
        if (len<0)
        {
            esyslog("markad [%i]: error decoding ac3",recvnumber);
            break;
        }
        else
        {
            SetAudioInfos(maContext,ac3_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
    }
    return ret;
#else
    return true;
#endif
}

#ifdef HAVE_AVCODEC
void cMarkAdDecoder::PAR2DAR(AVRational a, AVRational *erg)
{
    av_reduce(&erg->num,&erg->den,video_context->width*a.num,
              video_context->height*a.den,1024*1024);
}

bool cMarkAdDecoder::SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context, AVFrame *Video_Frame, AVRational *DAR)
{
    for (int i=0; i<4; i++)
    {
        if (temp_pictureplane[i])
        {
            free(temp_pictureplane[i]);
            temp_pictureplane[i]=NULL;
        }
    }

    if ((!maContext) || (!Video_Context) || (!Video_Frame)) return false;
    maContext->Video.Data.Valid=false;
    for (int i=0; i<4; i++)
    {
        if (Video_Frame->data[i])
        {
            temp_pictureplane[i]=(uchar *) malloc(Video_Frame->linesize[i]);
            if (!temp_pictureplane[i]) return false;
            memcpy(temp_pictureplane[i],Video_Frame->data[i],Video_Frame->linesize[i]);
            maContext->Video.Data.Plane[i]=temp_pictureplane[i];
            maContext->Video.Data.PlaneLinesize[i]=Video_Frame->linesize[i];
        }
    }
    maContext->Video.Info.Height=Video_Context->height;
    maContext->Video.Info.Width=Video_Context->width;

    switch (Video_Frame->pict_type)
    {
    case FF_I_TYPE:
        maContext->Video.Info.Pict_Type=MA_I_TYPE;
        break;
    case FF_P_TYPE:
        maContext->Video.Info.Pict_Type=MA_P_TYPE;
        break;
    case FF_B_TYPE:
        maContext->Video.Info.Pict_Type=MA_B_TYPE;
        break;
    case FF_S_TYPE:
        maContext->Video.Info.Pict_Type=MA_S_TYPE;
        break;
    case FF_SI_TYPE:
        maContext->Video.Info.Pict_Type=MA_SI_TYPE;
        break;
    case FF_SP_TYPE:
        maContext->Video.Info.Pict_Type=MA_SP_TYPE;
        break;
    case FF_BI_TYPE:
        maContext->Video.Info.Pict_Type=MA_BI_TYPE;
        break;
    default:
        maContext->Video.Info.Pict_Type=0;
    }

    if (DAR)
    {
        maContext->Video.Info.AspectRatio.Num=DAR->num;
        maContext->Video.Info.AspectRatio.Den=DAR->den;
    }

    maContext->Video.Data.Valid=true;
    return true;
}
#endif

bool cMarkAdDecoder::DecodeVideo(MarkAdContext *maContext,uchar *pkt, int len)
{
#ifdef HAVE_AVCODEC
    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=pkt;
    avpkt.size=len;

    // decode video
    int video_frame_ready=0;
    int len,ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        len=avcodec_decode_video(video_context,video_frame,&video_frame_ready,
                                 avpkt.data,avpkt.size);
#else
        len=avcodec_decode_video2(video_context,video_frame,&video_frame_ready,
                                  &avpkt);
#endif
        if (len<0)
        {
            esyslog("markad [%i]: error decoding video",recvnumber);
            break;
        }
        else
        {
            avpkt.size-=len;
            avpkt.data+=len;
        }
        if (video_frame_ready)
        {
            AVRational dar;
            PAR2DAR(video_context->sample_aspect_ratio,&dar);
            if (SetVideoInfos(maContext,video_context,video_frame,&dar)) ret=true;
        }
    }
    return ret;
#else
    return true;
#endif
}

// taken from femon
int cMarkAdDecoder::nalUnescape(uint8_t *dst, const uint8_t *src, int len)
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
        count(len),
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
