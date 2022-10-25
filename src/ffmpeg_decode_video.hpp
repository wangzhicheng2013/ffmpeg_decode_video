#pragma once
#include <memory>
#include <functional>
extern "C" {
    #include "ffmpeg/libavcodec/avcodec.h"
    #include "ffmpeg/libavformat/avformat.h"
    #include "ffmpeg/libswscale/swscale.h"
    #include "ffmpeg/libavutil/imgutils.h"
    #include "ffmpeg/libavutil/avassert.h"
    #include "ffmpeg/libavutil/imgutils.h"
    #include "ffmpeg/libavutil/intreadwrite.h"
}
#include "color_log.hpp"
#include "file_utility.hpp"
using picture_process_type = std::function<void(const char *, size_t)>;
class ffmpeg_decode_video : public std::enable_shared_from_this<ffmpeg_decode_video> {
public:
    bool init() {
        format_ctx_ = avformat_alloc_context();
        if (nullptr == format_ctx_) {
            LOG_E("avformat_alloc_context failed!");
            return false;
        }
        av_frame_ = av_frame_alloc();
        if (nullptr == av_frame_) {
            LOG_E("av_frame_alloc failed!");
            return false;
        }
        return true;
    }
    bool decode(const char *video_path) {
        if (true == decode_ok_) {
            LOG_I("%s has been decoded!", video_path);
            return true;
        }
        if (nullptr == video_path) {
            LOG_E("video path is null!");
            return false;
        }
        // step1: open video
        int ret = avformat_open_input(&format_ctx_, video_path, nullptr, nullptr); 
        if (0 != ret) {
            LOG_E("avformat_open_input failed error code:%d", ret);
            return false;
        }
        LOG_I("avformat_open_input for:%s success!", video_path);
        // step 2 find stream info
        ret = avformat_find_stream_info(format_ctx_, nullptr);
        if (ret < 0) {
            LOG_E("avformat_find_stream_info failed error code:%d", ret);
            return false;
        }
        LOG_I("avformat_find_stream_info success!");
        // step3 traverse to find a stream of video type
        for (uint i = 0;i < format_ctx_->nb_streams;i++) {
            LOG_I("codec_type:%d", format_ctx_->streams[i]->codecpar->codec_type);
            if (AVMEDIA_TYPE_VIDEO == format_ctx_->streams[i]->codecpar->codec_type) {
                video_stream_index_ = i;
                break;
            }
        }
        if (-1 == video_stream_index_) {
            LOG_E("can't find video stream!");
            return false;
        }
        LOG_I("find video stream index:%d success!", video_stream_index_);
        // step4 find decoder
        codec_ = (AVCodec*)avcodec_find_decoder(format_ctx_->streams[video_stream_index_]->codecpar->codec_id);
        if (nullptr == codec_) {
            LOG_E("can't find corresponding decoder!");
            return false;
        }
        LOG_I("avcodec_find_decoder success!");
        // step5 create decoder content according to decoder parameters
        codec_ctx_ = avcodec_alloc_context3(codec_);
        if (nullptr == codec_ctx_) {
            LOG_E("avcodec_alloc_context3 failed!");
            return false;
        }
        ret = avcodec_parameters_to_context(codec_ctx_, format_ctx_->streams[video_stream_index_]->codecpar);
        if (ret < 0) {
            LOG_E("avcodec_parameters_to_context failed error code:%d", ret);
            return false;
        }
        LOG_I("avcodec_parameters_to_context success!\n");
        // step6 configure the number of decoding threads
        codec_ctx_->thread_count = thread_num_;
        // step7 open decoder
        ret = avcodec_open2(codec_ctx_, codec_, nullptr);
        if (ret < 0) {
            LOG_E("avcodec_open2 failed error code:%d", ret);
            return false;
        }
        LOG_I("avcodec_open2 success!");
        // step8 allocate packet
        packet_ = (AVPacket*)malloc(sizeof(AVPacket));
        if (nullptr == packet_) {
            LOG_E("AVPacket malloc failed!");
            return false;
        }
        decode_ok_ = true;
        LOG_I("decode video for:%s is already ok!", video_path);
        return true;
    }
    bool output_pictures(picture_process_type processor, const char *store_path, uint format = ASVL_PAF_UYVY) {
        int ret = 0;
        size_t frame_size = 0;
        int frame_index = 0;
        const unsigned char *frame_data = nullptr;
        ASVLOFFSCREEN src_img = { 0 };
        ASVLOFFSCREEN dst_img = { 0 };
        bool dst_img_malloced = false; 
        if (false == decode_ok_) {
            LOG_E("decode video first!");
            return false;
        }
        for (;true;av_packet_unref(packet_)) {                      // break when av_read_frame failed
            ret = av_read_frame(format_ctx_, packet_);              // packet_ stores each frame of image data
            if (ret < 0) {
                LOG_E("av_read_frame failed error code:%d", ret);
                return false;
            }
            LOG_I("get stream_index:%d", packet_->stream_index);
            if (video_stream_index_ != packet_->stream_index) {
                continue;
            }
            if (false == get_decoded_frame()) {
                continue;
            }
            frame_size = sizeof(MUInt8) * av_frame_->width * av_frame_->height * 2;
            if (false == dst_img_malloced) {
                dst_img.ppu8Plane[0] = (MUInt8 *)malloc(frame_size);
                if (nullptr == dst_img.ppu8Plane[0]) {
                    LOG_E("malloc failed!");
                    return false;
                }
                dst_img_malloced = true;
            }
            if (ASVL_PAF_UYVY == format) {
                frame_data = convert_i402_to_uyvy(&src_img, &dst_img, frame_size);
                if (nullptr == frame_data) {
                    continue;
                }
                frame_index++;
                LOG_I("get frame index:%d", frame_index);
            }
            else {
                // not considered
            }
            if (store_path != nullptr) {
                write_uyvy_pictures(store_path, frame_data, frame_size, frame_index, dst_img.i32Width, dst_img.i32Height);
            }
            if (processor != nullptr) {
                processor((const char *)frame_data, frame_size);
            }
        }
        // get the remaining frames from mmfpeg queue
        packet_->data = nullptr;
        packet_->size = 0;
        while (true == get_decoded_frame()) {
            if (ASVL_PAF_UYVY == format) {
                frame_data = convert_i402_to_uyvy(&src_img, &dst_img, frame_size);
                if (nullptr == frame_data) {
                    continue;
                }
                frame_index++;
                LOG_I("get frame index:%d", frame_index);
            }
            if (store_path != nullptr) {
                write_uyvy_pictures(store_path, frame_data, frame_size, frame_index, dst_img.i32Width, dst_img.i32Height);
            }
            if (processor != nullptr) {
                processor((const char *)frame_data, frame_size);
            }
        }
        if (true == dst_img_malloced) {
            free(dst_img.ppu8Plane[0]);
        }
        return true;
    }
    std::shared_ptr<ffmpeg_decode_video> get_ptr() {
        return shared_from_this();
    }
    inline void set_thread_num(int num) {
        thread_num_  = num;
    }
    virtual ~ffmpeg_decode_video() {
        if (av_frame_ != nullptr) {
             av_frame_free(&av_frame_);
        }
        if (packet_ != nullptr) {
            av_packet_free(&packet_);
        }
        if (codec_ctx_ != nullptr) {
            avcodec_close(codec_ctx_);
            avcodec_free_context(&codec_ctx_);
        }
        if (format_ctx_ != nullptr) {
            avformat_close_input(&format_ctx_);
        }
    }
private:
    bool get_decoded_frame() {
        int ret = avcodec_send_packet(codec_ctx_, packet_);
        if (ret < 0) {
            LOG_E("avcodec_send_packet failed error code:%d", ret);
            return false;
        }
        ret = avcodec_receive_frame(codec_ctx_, av_frame_);
        if (-11 == ret) {       // not receive frame
            LOG_E("not yet receive frame from code context, please wait...!");
            return false;
        }
        else if (ret) {
            LOG_E("avcodec_receive_frame failed error code:%d", ret);
            return false;
        }
        return true;
    }
    const unsigned char *convert_i402_to_uyvy(LPASVLOFFSCREEN pSrcImg, LPASVLOFFSCREEN pDstImg, size_t frame_size) {
        pSrcImg->i32Width = av_frame_->width;
        pSrcImg->i32Height = av_frame_->height;
        pSrcImg->u32PixelArrayFormat = ASVL_PAF_I420;
        pSrcImg->pi32Pitch[0] = av_frame_->linesize[0];
        pSrcImg->pi32Pitch[1] = av_frame_->linesize[1];
        pSrcImg->pi32Pitch[2] = av_frame_->linesize[2];
        pSrcImg->ppu8Plane[0] = av_frame_->data[0];
        pSrcImg->ppu8Plane[1] = av_frame_->data[1];
        pSrcImg->ppu8Plane[2] = av_frame_->data[2];

        pDstImg->i32Width = av_frame_->width;
        pDstImg->i32Height = av_frame_->height;
        pDstImg->u32PixelArrayFormat = ASVL_PAF_UYVY;
        pDstImg->pi32Pitch[0] = pDstImg->i32Width * 2;
        pDstImg->pi32Pitch[1] = 0;
        pDstImg->pi32Pitch[2] = 0;
        int ret = mcvColorI420toUYVY(pSrcImg, pDstImg);
        if (ret) {
            LOG_E("mcvColorI420toUYVY failed error code:%d", ret);
            return nullptr;
        }
        LOG_I("convert to uyvy success!");
        return pDstImg->ppu8Plane[0];
    }
    void write_uyvy_pictures(const char *store_path, 
                             const unsigned char *frame_data, 
                             size_t frame_size, 
                             int frame_index,
                             int width,
                             int height) {
        char picture_path[128] = "";
        // such as /usr/tmp/uyvy_1277888881_100_1280x800.uyvy
        snprintf(picture_path, sizeof(picture_path), "%s/uyvy_%ld_%d_%dx%d.uyvy", store_path, time(NULL), frame_index, width, height);
        if (false == G_FILE_UTILITY.write_file_content(picture_path, (const char *)frame_data, frame_size)) {
            LOG_E("picture:%s write failed!", picture_path);
        }
        else {
            LOG_E("picture:%s write success!", picture_path);
        }
    }
private:
    AVFormatContext*   format_ctx_ = nullptr;
    AVCodec*           codec_ = nullptr;
    AVCodecContext*    codec_ctx_ = nullptr;
    AVFrame*           av_frame_ = nullptr;
    AVPacket*          packet_ = nullptr;
    int video_stream_index_ = -1;
    int thread_num_ = 4;
    bool decode_ok_ = false;
};