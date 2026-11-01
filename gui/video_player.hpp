// Plays Windows Media (WMV3 + WMA) movies: Media Foundation decodes on a worker thread, frames
// go to an OpenGL texture and sound to an SDL audio stream, paced by the audio clock.
#pragma once

#include "common.hpp"
#include "gl_view.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

struct SDL_AudioStream;

namespace nut::gui {

class VideoPlayer {
public:
    VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;
    ~VideoPlayer();

    // Opens a movie held in memory; `error()` explains a failure.
    bool open(Bytes data);
    void close();
    bool isOpen() const { return open_; }
    const std::string& error() const { return error_; }

    void play();
    void pause();
    bool playing() const { return playing_; }
    void seek(double seconds);
    void setVolume(float v);
    void setLoop(bool loop) { loop_ = loop; }

    double duration() const { return duration_; }
    double position() const; // seconds
    int width() const { return width_; }
    int height() const { return height_; }
    bool hasAudio() const { return audio_ != nullptr; }

    // Uploads the frame due now; call once per UI frame. Returns the texture (null before the first frame).
    const GlTexture* update();

private:
    struct Frame {
        double time = 0;
        std::vector<uint8_t> rgba;
    };
    void worker();
    void stopWorker();
    void startWorker();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    Bytes data_;
    bool open_ = false;
    std::string error_;
    int width_ = 0, height_ = 0;
    double duration_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_{false}, playing_{false}, ended_{false}, loop_{false};
    std::mutex m_;
    std::deque<Frame> frames_;
    double startPos_ = 0;          // stream position where the clock starts
    double clockBase_ = 0;         // wall-clock fallback when there is no audio
    uint64_t clockStart_ = 0;
    std::atomic<int64_t> audioBytes_{0}; // bytes queued to the audio stream since the last seek
    int audioRate_ = 44100, audioChannels_ = 2;
    SDL_AudioStream* audio_ = nullptr;
    float volume_ = 1;
    GlTexture tex_;
    bool haveFrame_ = false;
    double shownTime_ = -1;
};

} // namespace nut::gui
