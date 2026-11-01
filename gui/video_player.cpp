#include "video_player.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>

#include <algorithm>
#include <chrono>
#include <map>

namespace nut::gui {
namespace {

template <class T> void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Media Foundation is started once per process (and never shut down: other players may be open).
bool startMediaFoundation() {
    static bool ok = [] {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        (void)hr; // RPC_E_CHANGED_MODE is fine: COM is already initialised on this thread
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }();
    return ok;
}

} // namespace

struct VideoPlayer::Impl {
    IMFSourceReader* reader = nullptr;
    std::map<DWORD, bool> isVideo; // stream index -> video (false: audio)
    ~Impl() { release(reader); }
};

VideoPlayer::VideoPlayer() = default;
VideoPlayer::~VideoPlayer() { close(); }

bool VideoPlayer::open(Bytes data) {
    close();
    error_.clear();
    data_ = std::move(data);
    if (!startMediaFoundation()) {
        error_ = "Media Foundation is not available";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    IStream* mem = SHCreateMemStream(data_.data(), UINT(data_.size()));
    IMFByteStream* bs = nullptr;
    IMFAttributes* bsAttr = nullptr;
    IMFAttributes* attr = nullptr;
    IMFMediaType* type = nullptr;
    HRESULT hr = mem ? MFCreateMFByteStreamOnStream(mem, &bs) : E_OUTOFMEMORY;
    // XMV files have no telling extension: say what they are.
    if (SUCCEEDED(hr) && SUCCEEDED(bs->QueryInterface(IID_PPV_ARGS(&bsAttr)))) bsAttr->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/x-ms-asf");
    if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr)) attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromByteStream(bs, attr, &impl_->reader);
    release(attr);
    release(bsAttr);
    release(bs);
    release(mem);
    if (FAILED(hr)) {
        error_ = "Windows could not open this movie (error " + hex32(uint32_t(hr)) + ")";
        impl_.reset();
        return false;
    }
    IMFSourceReader* r = impl_->reader;
    // Video as 32-bit BGRX frames.
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = r->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
    release(type);
    if (FAILED(hr)) {
        error_ = "no decodable video stream (error " + hex32(uint32_t(hr)) + ")";
        impl_.reset();
        return false;
    }
    if (SUCCEEDED(r->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type))) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
        width_ = int(w);
        height_ = int(h);
        release(type);
    }
    // Audio as 16-bit PCM; a movie without decodable sound still plays.
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr = r->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, type);
    release(type);
    if (SUCCEEDED(hr) && SUCCEEDED(r->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &type))) {
        audioRate_ = int(MFGetAttributeUINT32(type, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100));
        audioChannels_ = int(MFGetAttributeUINT32(type, MF_MT_AUDIO_NUM_CHANNELS, 2));
        release(type);
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_AudioSpec spec{SDL_AUDIO_S16, audioChannels_, audioRate_};
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (audio_) SDL_SetAudioStreamGain(audio_, volume_);
        }
    }
    if (!audio_) r->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM), FALSE);
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(r->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var))) duration_ = double(var.uhVal.QuadPart) / 1e7;
    PropVariantClear(&var);
    open_ = true;
    seek(0);
    return true;
}

void VideoPlayer::close() {
    stopWorker();
    if (audio_) SDL_DestroyAudioStream(audio_);
    audio_ = nullptr;
    impl_.reset();
    frames_.clear();
    open_ = false;
    playing_ = false;
    haveFrame_ = false;
}

void VideoPlayer::stopWorker() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    stop_ = false;
}

void VideoPlayer::startWorker() {
    stop_ = false;
    thread_ = std::thread([this] { worker(); });
}

void VideoPlayer::worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMFSourceReader* r = impl_->reader;
    const double bytesPerSecond = double(audioRate_) * audioChannels_ * 2;
    while (!stop_) {
        // Stay a little ahead of playback: a few frames and up to a second of sound.
        size_t queued;
        {
            std::lock_guard<std::mutex> lk(m_);
            queued = frames_.size();
        }
        bool audioFull = !audio_ || SDL_GetAudioStreamQueued(audio_) > int(bytesPerSecond);
        if (queued >= 8 && audioFull) {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample* sample = nullptr;
        HRESULT hr = r->ReadSample(DWORD(MF_SOURCE_READER_ANY_STREAM), 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            ended_ = true;
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            // With both streams selected, the reader reports each end; stop after the video.
            IMFMediaType* t = nullptr;
            GUID major{};
            if (SUCCEEDED(r->GetCurrentMediaType(stream, &t))) { t->GetGUID(MF_MT_MAJOR_TYPE, &major); t->Release(); }
            if (major == MFMediaType_Video) {
                ended_ = true;
                release(sample);
                break;
            }
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            IMFMediaType* t = nullptr;
            if (SUCCEEDED(r->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &t))) {
                UINT32 w = 0, h = 0;
                MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
                width_ = int(w);
                height_ = int(h);
                t->Release();
            }
        }
        if (!sample) continue;
        auto kind = impl_->isVideo.find(stream);
        if (kind == impl_->isVideo.end()) {
            IMFMediaType* t = nullptr;
            GUID major{};
            if (SUCCEEDED(r->GetCurrentMediaType(stream, &t))) { t->GetGUID(MF_MT_MAJOR_TYPE, &major); t->Release(); }
            kind = impl_->isVideo.emplace(stream, major == MFMediaType_Video).first;
        }
        double time = double(ts) / 1e7;
        IMFMediaBuffer* buf = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
            if (kind->second) {
                // BGRX rows (pitch may be padded or negative) -> RGBA.
                Frame f;
                f.time = time;
                f.rgba.resize(size_t(width_) * height_ * 4);
                IMF2DBuffer* b2 = nullptr;
                BYTE* base = nullptr;
                LONG pitch = 0;
                DWORD len = 0;
                bool locked2d = SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&base, &pitch));
                if (!locked2d && SUCCEEDED(buf->Lock(&base, nullptr, &len))) pitch = width_ * 4;
                if (base) {
                    for (int y = 0; y < height_; ++y) {
                        const uint8_t* src = base + ptrdiff_t(y) * pitch;
                        uint8_t* dst = &f.rgba[size_t(y) * width_ * 4];
                        for (int x = 0; x < width_; ++x) {
                            dst[4 * x] = src[4 * x + 2];
                            dst[4 * x + 1] = src[4 * x + 1];
                            dst[4 * x + 2] = src[4 * x];
                            dst[4 * x + 3] = 255;
                        }
                    }
                    if (locked2d) b2->Unlock2D();
                    else buf->Unlock();
                    if (time + 0.05 >= startPos_) {
                        std::lock_guard<std::mutex> lk(m_);
                        frames_.push_back(std::move(f));
                    }
                }
                release(b2);
            } else if (audio_) {
                BYTE* p = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(buf->Lock(&p, nullptr, &len))) {
                    // After a seek, skip sound from before the target (the reader starts at a key frame).
                    double skip = startPos_ - time;
                    size_t offset = 0;
                    if (skip > 0) offset = std::min<size_t>(len, size_t(skip * bytesPerSecond) & ~size_t(3));
                    if (offset < len) {
                        SDL_PutAudioStreamData(audio_, p + offset, int(len - offset));
                        audioBytes_ += int64_t(len - offset);
                    }
                    buf->Unlock();
                }
            }
            buf->Release();
        }
        sample->Release();
    }
    CoUninitialize();
}

void VideoPlayer::play() {
    if (!open_) return;
    bool restart;
    {
        std::lock_guard<std::mutex> lk(m_);
        restart = ended_ && frames_.empty();
    }
    if (restart) seek(0);
    clockBase_ = position();
    clockStart_ = SDL_GetTicksNS();
    playing_ = true;
    if (audio_) SDL_ResumeAudioStreamDevice(audio_);
}

void VideoPlayer::pause() {
    if (!open_) return;
    clockBase_ = position();
    playing_ = false;
    if (audio_) SDL_PauseAudioStreamDevice(audio_);
}

void VideoPlayer::seek(double seconds) {
    if (!impl_) return;
    stopWorker();
    seconds = std::clamp(seconds, 0.0, std::max(0.0, duration_ - 0.05));
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = LONGLONG(seconds * 1e7);
    impl_->reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    {
        std::lock_guard<std::mutex> lk(m_);
        frames_.clear();
    }
    if (audio_) SDL_ClearAudioStream(audio_);
    audioBytes_ = 0;
    startPos_ = clockBase_ = seconds;
    clockStart_ = SDL_GetTicksNS();
    ended_ = false;
    haveFrame_ = false;
    startWorker();
}

void VideoPlayer::setVolume(float v) {
    volume_ = v;
    if (audio_) SDL_SetAudioStreamGain(audio_, v);
}

double VideoPlayer::position() const {
    if (!open_) return 0;
    if (audio_) {
        // Sound actually handed to the device so far, after the last seek.
        int64_t played = audioBytes_ - SDL_GetAudioStreamQueued(audio_);
        double t = startPos_ + double(std::max<int64_t>(0, played)) / (double(audioRate_) * audioChannels_ * 2);
        return std::min(t, duration_);
    }
    if (!playing_) return clockBase_;
    return std::min(duration_, clockBase_ + double(SDL_GetTicksNS() - clockStart_) / 1e9);
}

const GlTexture* VideoPlayer::update() {
    if (!open_) return nullptr;
    double pos = position();
    Frame show;
    bool got = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        // Drop frames that are already late; show the newest one that is due.
        while (frames_.size() > 1 && frames_[1].time <= pos) frames_.pop_front();
        if (!frames_.empty() && (frames_.front().time <= pos + 0.01 || !haveFrame_)) {
            show = std::move(frames_.front());
            frames_.pop_front();
            got = true;
        }
    }
    if (got && !show.rgba.empty()) {
        tex_.upload(show.rgba.data(), width_, height_, false, false);
        haveFrame_ = true;
        shownTime_ = show.time;
    }
    if (ended_ && playing_) {
        bool drained;
        {
            std::lock_guard<std::mutex> lk(m_);
            drained = frames_.empty();
        }
        if (drained && (!audio_ || SDL_GetAudioStreamQueued(audio_) == 0)) {
            if (loop_) {
                seek(0);
                play();
            } else {
                pause();
            }
        }
    }
    return haveFrame_ ? &tex_ : nullptr;
}

} // namespace nut::gui
