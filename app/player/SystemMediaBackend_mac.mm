#include "player/SystemMediaBackend.h"
#include "player/SystemMediaArtwork.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#include <atomic>
#include <cstring>

namespace {
struct NativeContext {
    SystemMediaCallback callback;
    std::atomic<quint64> session{0};
    std::atomic<bool> active{false};
};

class MacMediaBackend final : public SystemMediaBackend {
public:
    explicit MacMediaBackend(SystemMediaCallback callback)
        : context_(std::make_shared<NativeContext>()) {
        context_->callback = std::move(callback);
        commands_ = [NSMutableArray array];
        targets_ = [NSMutableArray array];
        auto *center = [MPRemoteCommandCenter sharedCommandCenter];
        add(center.playCommand, SystemMediaCommand::Play);
        add(center.pauseCommand, SystemMediaCommand::Pause);
        add(center.togglePlayPauseCommand, SystemMediaCommand::Toggle);
        add(center.previousTrackCommand, SystemMediaCommand::Previous);
        add(center.nextTrackCommand, SystemMediaCommand::Next);
        add(center.changePlaybackPositionCommand, SystemMediaCommand::Seek);
        add(center.stopCommand, SystemMediaCommand::Stop);
        QObject::connect(&cover_, &SystemMediaArtwork::imageChanged, &cover_, [this] {
            refreshArtwork();
            publish();
        });
    }

    ~MacMediaBackend() override {
        update({});
        for (NSUInteger i = 0; i < commands_.count; ++i)
            [commands_[i] removeTarget:targets_[i]];
    }

    void setWindow(quintptr) override {}

    void update(const SystemMediaState &state) override {
        state_ = state;
        cover_.setSource(state.active ? state.session : 0,
                         state.active ? QUrl(state.coverUrl) : QUrl{});
        context_->active.store(false);
        context_->session.store(state.session);
        context_->active.store(state.active);
        auto *commands = [MPRemoteCommandCenter sharedCommandCenter];
        commands.playCommand.enabled = state.active;
        commands.pauseCommand.enabled = state.active;
        commands.togglePlayPauseCommand.enabled = state.active;
        commands.stopCommand.enabled = state.active;
        commands.previousTrackCommand.enabled = state.active && state.canPrevious;
        commands.nextTrackCommand.enabled = state.active && state.canNext;
        commands.changePlaybackPositionCommand.enabled = state.active && state.canSeek;
        publish();
    }

private:
    void refreshArtwork() {
        artwork_ = nil;
        const QImage &pixels = cover_.image();
        if (pixels.isNull()) return;
        NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr pixelsWide:pixels.width() pixelsHigh:pixels.height()
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
            bytesPerRow:pixels.bytesPerLine() bitsPerPixel:32];
        if (!bitmap) return;
        std::memcpy(bitmap.bitmapData, pixels.constBits(), pixels.sizeInBytes());
        NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(pixels.width(), pixels.height())];
        [image addRepresentation:bitmap];
        artwork_ = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
            requestHandler:^NSImage *(CGSize) { return image; }];
    }

    void publish() {
        const auto &state = state_;
        auto *info = [MPNowPlayingInfoCenter defaultCenter];
        if (!state.active) {
            info.nowPlayingInfo = nil;
            info.playbackState = MPNowPlayingPlaybackStateStopped;
            return;
        }
        const double effectiveRate = state.paused || state.buffering ? 0 : state.rate;
        NSMutableDictionary *metadata = [@{
            MPMediaItemPropertyTitle: state.title.toNSString(),
            MPMediaItemPropertyArtist: state.artist.toNSString(),
            MPMediaItemPropertyPlaybackDuration: @(state.duration),
            MPNowPlayingInfoPropertyElapsedPlaybackTime: @(state.position),
            MPNowPlayingInfoPropertyPlaybackRate: @(effectiveRate),
            MPNowPlayingInfoPropertyDefaultPlaybackRate: @(state.rate),
            MPNowPlayingInfoPropertyMediaType: @(MPNowPlayingInfoMediaTypeVideo)
        } mutableCopy];
        // Retain the same decoded artwork through regular position updates.
        if (artwork_) metadata[MPMediaItemPropertyArtwork] = artwork_;
        info.nowPlayingInfo = metadata;
        info.playbackState = state.paused || state.buffering
            ? MPNowPlayingPlaybackStatePaused : MPNowPlayingPlaybackStatePlaying;
    }

    void add(MPRemoteCommand *command, SystemMediaCommand action) {
        const auto context = context_;
        id token = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
            if (!context->active.load()) return MPRemoteCommandHandlerStatusNoSuchContent;
            const quint64 session = context->session.load();
            const double position = action == SystemMediaCommand::Seek
                ? [(MPChangePlaybackPositionCommandEvent *)event positionTime] : 0;
            context->callback(action, position, session);
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        command.enabled = NO;
        [commands_ addObject:command];
        [targets_ addObject:token];
    }

    std::shared_ptr<NativeContext> context_;
    NSMutableArray<MPRemoteCommand *> *commands_;
    NSMutableArray *targets_;
    SystemMediaState state_;
    SystemMediaArtwork cover_;
    MPMediaItemArtwork *artwork_ = nil;
};
}

std::unique_ptr<SystemMediaBackend> createSystemMediaBackend(SystemMediaCallback callback) {
    return std::make_unique<MacMediaBackend>(std::move(callback));
}
