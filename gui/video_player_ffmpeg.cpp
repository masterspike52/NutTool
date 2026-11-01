// VideoPlayer decoder for Linux and other non-Windows systems: FFmpeg, reading the movie from memory.
#include "video_player.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nut::gui {

namespace {

std::string avError(int err) {
    char t[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, t, sizeof t);
    return t;
}

// The movie in memory, as FFmpeg's I/O callbacks read it.
struct MemFile {
    const Bytes* data = nullptr;
    int64_t pos = 0;
};

int readData(void* opaque, uint8_t* buf, int size) {
    auto& im = *static_cast<MemFile*>(opaque);
    int64_t left = int64_t(im.data->size()) - im.pos;
    if (left <= 0) return AVERROR_EOF;
    int n = int(std::min<int64_t>(size, left));
    std::memcpy(buf, im.data->data() + im.pos, size_t(n));
    im.pos += n;
    return n;
}

int64_t seekData(void* opaque, int64_t offset, int whence) {
    auto& im = *static_cast<MemFile*>(opaque);
    int64_t size = int64_t(im.data->size());
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return size;
    case SEEK_SET: break;
    case SEEK_CUR: offset += im.pos; break;
    case SEEK_END: offset += size; break;
    default: return -1;
    }
    if (offset < 0 || offset > size) return -1;
    im.pos = offset;
    return offset;
}

// Opens the decoder for the best stream of `type`; returns the stream index or a negative error.
int openDecoder(AVFormatContext* fmt, AVMediaType type, AVCodecContext*& ctx) {
    const AVCodec* codec = nullptr;
    int s = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
    if (s < 0) return s;
    ctx = avcodec_alloc_context3(codec);
    int err = ctx ? avcodec_parameters_to_context(ctx, fmt->streams[s]->codecpar) : AVERROR(ENOMEM);
    if (err >= 0) err = avcodec_open2(ctx, codec, nullptr);
    if (err < 0) {
        avcodec_free_context(&ctx);
        return err;
    }
    return s;
}

} // namespace

struct VideoPlayer::Impl {
    MemFile file;
    AVIOContext* io = nullptr;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    int videoStream = -1, audioStream = -1;
    double start = 0; // container start time, seconds
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    std::vector<uint8_t> pcm;

    ~Impl() {
        sws_freeContext(sws);
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&fmt);
        if (io) av_freep(&io->buffer);
        avio_context_free(&io);
    }
};

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() { close(); }

void VideoPlayer::closeSource() { impl_.reset(); }

bool VideoPlayer::openSource() {
    av_log_set_level(AV_LOG_ERROR);
    impl_ = std::make_unique<Impl>();
    Impl& im = *impl_;
    im.file.data = &data_;
    constexpr int ioSize = 64 * 1024;
    auto* buf = static_cast<unsigned char*>(av_malloc(ioSize));
    im.io = buf ? avio_alloc_context(buf, ioSize, 0, &im.file, readData, nullptr, seekData) : nullptr;
    if (!im.io) av_free(buf);
    im.fmt = avformat_alloc_context();
    if (!im.io || !im.fmt) {
        error_ = "out of memory";
        return false;
    }
    im.fmt->pb = im.io;
    im.fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    int err = avformat_open_input(&im.fmt, nullptr, nullptr, nullptr); // frees fmt on failure
    if (err >= 0) err = avformat_find_stream_info(im.fmt, nullptr);
    if (err < 0) {
        error_ = "FFmpeg could not open this movie (" + avError(err) + ")";
        return false;
    }
    im.videoStream = openDecoder(im.fmt, AVMEDIA_TYPE_VIDEO, im.video);
    if (im.videoStream < 0) {
        error_ = "no decodable video stream (" + avError(im.videoStream) + ")";
        return false;
    }
    width_ = im.video->width;
    height_ = im.video->height;
    if (im.fmt->start_time != AV_NOPTS_VALUE) im.start = double(im.fmt->start_time) / AV_TIME_BASE;
    if (im.fmt->duration > 0) duration_ = double(im.fmt->duration) / AV_TIME_BASE;
    else if (AVStream* st = im.fmt->streams[im.videoStream]; st->duration > 0) duration_ = double(st->duration) * av_q2d(st->time_base);

    // Audio as interleaved 16-bit PCM at its own rate; a movie without decodable sound still plays.
    im.audioStream = openDecoder(im.fmt, AVMEDIA_TYPE_AUDIO, im.audio);
    if (im.audioStream >= 0 && im.audio->ch_layout.nb_channels > 0) {
        audioRate_ = im.audio->sample_rate;
        audioChannels_ = std::min(im.audio->ch_layout.nb_channels, 8);
        AVChannelLayout out;
        av_channel_layout_default(&out, audioChannels_);
        bool ok = swr_alloc_set_opts2(&im.swr, &out, AV_SAMPLE_FMT_S16, audioRate_, &im.audio->ch_layout, im.audio->sample_fmt,
                                      im.audio->sample_rate, 0, nullptr) >= 0 &&
                  swr_init(im.swr) >= 0;
        av_channel_layout_uninit(&out);
        if (!ok || !openAudio()) {
            swr_free(&im.swr);
            avcodec_free_context(&im.audio);
            im.audioStream = -1;
        }
    } else {
        avcodec_free_context(&im.audio);
        im.audioStream = -1;
    }
    for (unsigned s = 0; s < im.fmt->nb_streams; ++s)
        if (int(s) != im.videoStream && int(s) != im.audioStream) im.fmt->streams[s]->discard = AVDISCARD_ALL;
    im.packet = av_packet_alloc();
    im.frame = av_frame_alloc();
    if (!im.packet || !im.frame) {
        error_ = "out of memory";
        return false;
    }
    return true;
}

void VideoPlayer::seekSource(double seconds) {
    Impl& im = *impl_;
    int64_t ts = int64_t((seconds + im.start) * AV_TIME_BASE);
    if (av_seek_frame(im.fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) avformat_seek_file(im.fmt, -1, INT64_MIN, ts, ts, 0);
    avcodec_flush_buffers(im.video);
    if (im.audio) avcodec_flush_buffers(im.audio);
    if (im.swr) swr_init(im.swr); // drop samples held back for resampling
}

void VideoPlayer::worker() {
    Impl& im = *impl_;
    // Sends one packet (null: end of stream) and hands on every frame the decoder returns.
    auto decode = [&](AVCodecContext* c, const AVPacket* p) {
        if (avcodec_send_packet(c, p) < 0) return;
        bool isVideo = c == im.video;
        AVStream* st = im.fmt->streams[isVideo ? im.videoStream : im.audioStream];
        while (!stop_ && avcodec_receive_frame(c, im.frame) >= 0) {
            AVFrame* fr = im.frame;
            int64_t pts = fr->best_effort_timestamp;
            double time = pts == AV_NOPTS_VALUE ? 0 : double(pts) * av_q2d(st->time_base) - im.start;
            if (isVideo) {
                if (fr->width != width_ || fr->height != height_) {
                    width_ = fr->width;
                    height_ = fr->height;
                }
                im.sws = sws_getCachedContext(im.sws, fr->width, fr->height, AVPixelFormat(fr->format), fr->width, fr->height, AV_PIX_FMT_RGBA,
                                              SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!im.sws) continue;
                Frame f;
                f.time = time;
                f.rgba.resize(size_t(fr->width) * fr->height * 4);
                uint8_t* dst[4] = {f.rgba.data()};
                int pitch[4] = {fr->width * 4};
                sws_scale(im.sws, fr->data, fr->linesize, 0, fr->height, dst, pitch);
                pushFrame(std::move(f));
            } else {
                int room = swr_get_out_samples(im.swr, fr->nb_samples);
                if (room <= 0) continue;
                im.pcm.resize(size_t(room) * audioChannels_ * 2);
                uint8_t* dst[1] = {im.pcm.data()};
                int n = swr_convert(im.swr, dst, room, const_cast<const uint8_t**>(fr->extended_data), fr->nb_samples);
                if (n > 0) pushAudio(im.pcm.data(), size_t(n) * audioChannels_ * 2, time);
            }
        }
    };
    while (!stop_) {
        if (bufferFull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        if (av_read_frame(im.fmt, im.packet) < 0) {
            // End of the file (or damage): take what the decoders still hold, then stop.
            decode(im.video, nullptr);
            if (im.audio) decode(im.audio, nullptr);
            ended_ = true;
            break;
        }
        if (im.packet->stream_index == im.videoStream) decode(im.video, im.packet);
        else if (im.packet->stream_index == im.audioStream) decode(im.audio, im.packet);
        av_packet_unref(im.packet);
    }
}

} // namespace nut::gui
