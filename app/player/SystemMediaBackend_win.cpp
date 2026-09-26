#include "SystemMediaBackend.h"
#include "SystemMediaWindowsSmoke.h"

#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

// WinRT ABI declarations are kept local because Qt's MinGW 13.1 SDK has only
// partial windows.media.h declarations (no Controls2, timeline or video properties).
// Method order, signatures and IIDs follow mingw-w64's windows.media.idl/.h and
// systemmediatransportcontrolsinterop.idl, matching the Windows SDK contract:
// https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/windows.media.idl
// This requires neither C++/WinRT nor WRL and uses the same ABI with MSVC/MinGW.
// ABI interfaces must have external linkage: GCC can devirtualize abstract
// interfaces in an anonymous namespace to __cxa_pure_virtual at -O3 because
// it cannot see any concrete implementation. These implementations live in
// Windows DLLs and arrive through RoGetActivationFactory/QueryInterface.
namespace MediaAbi {
struct TimeSpan { INT64 Duration; };
struct EventRegistrationToken { INT64 value; };
enum MediaPlaybackStatus { Closed, Changing, Stopped, Playing, Paused };
enum MediaPlaybackType { Unknown, Music, Video, Image };
enum SoundLevel { Muted, Low, Full };
enum MediaPlaybackAutoRepeatMode { None, Track, List };
enum SystemMediaTransportControlsButton {
    Play, Pause, Stop, Record, FastForward, Rewind, Next, Previous, ChannelUp, ChannelDown
};
struct ISystemMediaTransportControls;
struct ISystemMediaTransportControlsDisplayUpdater;
struct ISystemMediaTransportControlsTimelineProperties;
struct ISystemMediaTransportControlsButtonPressedEventArgs;
struct IPlaybackPositionChangeRequestedEventArgs;
struct IVideoDisplayProperties;
struct IMusicDisplayProperties;
struct IImageDisplayProperties;
struct IRandomAccessStreamReference;
struct IStorageFile;
struct IAsyncOperationBoolean;
template<class Args> struct EventHandler : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(ISystemMediaTransportControls *, Args *) = 0;
};
struct UnusedEventArgs;
using ButtonHandler = EventHandler<ISystemMediaTransportControlsButtonPressedEventArgs>;
using PositionHandler = EventHandler<IPlaybackPositionChangeRequestedEventArgs>;
using UnusedHandler = EventHandler<UnusedEventArgs>;
struct ISystemMediaTransportControls : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(MediaPlaybackStatus *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(MediaPlaybackStatus value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(ISystemMediaTransportControlsDisplayUpdater **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(SoundLevel *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(
        ButtonHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(
        UnusedHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(EventRegistrationToken token) = 0;
};
struct ISystemMediaTransportControls2 : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_AutoRepeatMode(MediaPlaybackAutoRepeatMode *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AutoRepeatMode(MediaPlaybackAutoRepeatMode value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShuffleEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ShuffleEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackRate(DOUBLE *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackRate(DOUBLE value) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTimelineProperties(ISystemMediaTransportControlsTimelineProperties *timeline_properties) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackPositionChangeRequested(
        PositionHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackPositionChangeRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PlaybackRateChangeRequested(
        UnusedHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PlaybackRateChangeRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ShuffleEnabledChangeRequested(
        UnusedHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ShuffleEnabledChangeRequested(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AutoRepeatModeChangeRequested(
        UnusedHandler *handler,
        EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AutoRepeatModeChangeRequested(EventRegistrationToken token) = 0;
};
struct ISystemMediaTransportControlsDisplayUpdater : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Type(MediaPlaybackType *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(MediaPlaybackType value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IRandomAccessStreamReference **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(IRandomAccessStreamReference *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IMusicDisplayProperties **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(IVideoDisplayProperties **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(IImageDisplayProperties **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(
        MediaPlaybackType type,
        IStorageFile *source,
        IAsyncOperationBoolean **operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update() = 0;
};
struct IVideoDisplayProperties : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Subtitle(HSTRING value) = 0;
};
struct ISystemMediaTransportControlsTimelineProperties : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(TimeSpan *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StartTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(TimeSpan *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_EndTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(TimeSpan *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinSeekTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(TimeSpan *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MaxSeekTime(TimeSpan value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(TimeSpan *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Position(TimeSpan value) = 0;
};
struct IPlaybackPositionChangeRequestedEventArgs : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_RequestedPlaybackPosition(TimeSpan *value) = 0;
};
struct ISystemMediaTransportControlsButtonPressedEventArgs : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Button(SystemMediaTransportControlsButton *value) = 0;
};
struct ISystemMediaTransportControlsInterop : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND, REFIID, void **) = 0;
};
constexpr IID controlsIid{0x99fa3ff4, 0x1742, 0x42a6, {0x90,0x2e,0x08,0x7d,0x41,0xf9,0x65,0xec}};
constexpr IID controls2Iid{0xea98d2f6, 0x7f3c, 0x4af2, {0xa5,0x86,0x72,0x88,0x98,0x08,0xef,0xb1}};
constexpr IID timelineIid{0x5125316a, 0xc3a2, 0x475b, {0x85,0x07,0x93,0x53,0x4d,0xc8,0x8f,0x15}};
constexpr IID interopIid{0xddb0472d, 0xc911, 0x4a1f, {0x86,0xd9,0xdc,0x3d,0x71,0xa9,0x5f,0x5a}};
constexpr IID buttonHandlerIid{0x0557e996, 0x7b23, 0x5bae, {0xaa,0x81,0xea,0x0d,0x67,0x11,0x43,0xa4}};
constexpr IID positionHandlerIid{0x44e34f15, 0xbdc0, 0x50a7, {0xac,0xe4,0x39,0xe9,0x1f,0xb7,0x53,0xf1}};
} // namespace MediaAbi

namespace {
using namespace MediaAbi;

template<class T> class ComPtr {
public:
    ~ComPtr() { reset(); }
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    T *operator->() const { return value_; }
    T *get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    T **put() { reset(); return &value_; }
    void **putVoid() { return reinterpret_cast<void **>(put()); }
    void reset() { if (value_) { value_->Release(); value_ = nullptr; } }
private:
    T *value_ = nullptr;
};

class WinString {
public:
    explicit WinString(const QString &text) {
        WindowsCreateString(reinterpret_cast<const wchar_t *>(text.utf16()),
                            static_cast<UINT32>(text.size()), &value_);
    }
    ~WinString() { WindowsDeleteString(value_); }
    HSTRING get() const { return value_; }
private:
    HSTRING value_ = nullptr;
};

struct CallbackState {
    explicit CallbackState(SystemMediaCallback cb) : callback(std::move(cb)) {}
    SystemMediaCallback callback;
    std::atomic<quint64> epoch{0};
};

template<class Interface, const IID &interfaceIid> class HandlerBase : public Interface {
public:
    explicit HandlerBase(std::shared_ptr<CallbackState> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        // Handlers contain only immutable data and atomics, so events may arrive
        // directly on WinRT worker threads without an apartment-bound proxy.
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IAgileObject)
                || IsEqualIID(iid, interfaceIid)) {
            *object = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }
protected:
    virtual ~HandlerBase() = default;
    std::shared_ptr<CallbackState> state_;
private:
    std::atomic<ULONG> references_{1};
};

class ButtonCallback final : public HandlerBase<ButtonHandler, buttonHandlerIid> {
public:
    using HandlerBase::HandlerBase;
    HRESULT STDMETHODCALLTYPE Invoke(ISystemMediaTransportControls *,
            ISystemMediaTransportControlsButtonPressedEventArgs *args) override {
        const quint64 epoch = state_->epoch.load();
        if (!epoch || !args) return S_OK;
        SystemMediaTransportControlsButton button;
        if (FAILED(args->get_Button(&button))) return S_OK;
        switch (button) {
        case Play: state_->callback(SystemMediaCommand::Play, 0, epoch); break;
        case Pause: state_->callback(SystemMediaCommand::Pause, 0, epoch); break;
        case Stop: state_->callback(SystemMediaCommand::Stop, 0, epoch); break;
        case Next: state_->callback(SystemMediaCommand::Next, 0, epoch); break;
        case Previous: state_->callback(SystemMediaCommand::Previous, 0, epoch); break;
        default: break;
        }
        return S_OK;
    }
};

class PositionCallback final : public HandlerBase<PositionHandler, positionHandlerIid> {
public:
    using HandlerBase::HandlerBase;
    HRESULT STDMETHODCALLTYPE Invoke(ISystemMediaTransportControls *,
            IPlaybackPositionChangeRequestedEventArgs *args) override {
        const quint64 epoch = state_->epoch.load();
        if (!epoch || !args) return S_OK;
        TimeSpan position{};
        if (SUCCEEDED(args->get_RequestedPlaybackPosition(&position)))
            state_->callback(SystemMediaCommand::Seek, position.Duration / 10000000.0, epoch);
        return S_OK;
    }
};

TimeSpan toTimeSpan(double seconds) {
    // WinRT represents time in 100 ns units; clamp malformed/unknown durations.
    const double safe = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 9.0e11) : 0.0;
    return { static_cast<INT64>(safe * 10000000.0) };
}

class WindowsMediaBackend final : public SystemMediaBackend {
public:
    explicit WindowsMediaBackend(SystemMediaCallback callback)
        : callback_(std::move(callback)) {
        const HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
        uninitialize_ = SUCCEEDED(result);
        initialized_ = uninitialize_ || result == RPC_E_CHANGED_MODE;
        if (!initialized_)
            qWarning() << "Windows system media initialization failed; HRESULT" << Qt::hex << quint32(result);
    }
    ~WindowsMediaBackend() override {
        reset();
        if (uninitialize_) RoUninitialize();
    }
    void setWindow(quintptr window) override {
        const HWND hwnd = reinterpret_cast<HWND>(window);
        if (hwnd == window_) return;
        reset();
        window_ = hwnd;
        if (state_.active) publish();
    }
    void update(const SystemMediaState &state) override {
        state_ = state;
        if (!state.active) {
            reset();
            return;
        }
        publish();
    }
    // Read the real WinRT objects back: unavailable controls must not silently
    // pass an offline deployment check.
    QString verificationError() const {
        if (!state_.active)
            return controls_ || controls2_ || updater_ || timeline_
                ? QStringLiteral("SMTC reset retained native objects") : resetError_;
        if (!controls_ || !controls2_ || !updater_ || !timeline_
            || !buttonsRegistered_ || !positionRegistered_)
            return QStringLiteral("SMTC attach, timeline or event registration failed");
        boolean enabled = false;
        MediaPlaybackStatus status = Closed;
        if (FAILED(controls_->get_IsEnabled(&enabled)) || !enabled
            || FAILED(controls_->get_PlaybackStatus(&status))
            || status != (state_.buffering ? Changing : state_.paused ? Paused : Playing))
            return QStringLiteral("SMTC playback status readback failed");
        boolean next = false, previous = false;
        if (FAILED(controls_->get_IsNextEnabled(&next)) || next != state_.canNext
            || FAILED(controls_->get_IsPreviousEnabled(&previous)) || previous != state_.canPrevious)
            return QStringLiteral("SMTC navigation readback failed");
        MediaPlaybackType type = MediaAbi::Unknown;
        ComPtr<IVideoDisplayProperties> video;
        if (FAILED(updater_->get_Type(&type)) || type != Video
            || FAILED(updater_->get_VideoProperties(video.put())) || !video)
            return QStringLiteral("SMTC video metadata unavailable");
        HSTRING title = nullptr, artist = nullptr;
        const HRESULT titleResult = video->get_Title(&title);
        const HRESULT artistResult = video->get_Subtitle(&artist);
        auto text = [](HSTRING value) {
            UINT32 length = 0;
            const wchar_t *data = WindowsGetStringRawBuffer(value, &length);
            return QString::fromWCharArray(data, length);
        };
        const bool matches = SUCCEEDED(titleResult) && SUCCEEDED(artistResult)
            && text(title) == state_.title && text(artist) == state_.artist;
        WindowsDeleteString(title);
        WindowsDeleteString(artist);
        if (!matches) return QStringLiteral("SMTC metadata readback failed");
        DOUBLE rate = 0;
        TimeSpan start{}, end{}, minimum{}, maximum{}, position{};
        const double duration = std::max(0.0, state_.duration);
        const double expectedPosition = std::clamp(state_.position, 0.0, duration);
        if (FAILED(controls2_->get_PlaybackRate(&rate)) || rate != state_.rate
            || FAILED(timeline_->get_StartTime(&start)) || start.Duration != 0
            || FAILED(timeline_->get_EndTime(&end)) || end.Duration != toTimeSpan(duration).Duration
            || FAILED(timeline_->get_MinSeekTime(&minimum))
            || minimum.Duration != toTimeSpan(state_.canSeek ? 0 : expectedPosition).Duration
            || FAILED(timeline_->get_MaxSeekTime(&maximum))
            || maximum.Duration != toTimeSpan(state_.canSeek ? duration : expectedPosition).Duration
            || FAILED(timeline_->get_Position(&position))
            || position.Duration != toTimeSpan(expectedPosition).Duration)
            return QStringLiteral("SMTC timeline readback failed");
        return {};
    }
private:
    bool attach() {
        if (controls_) return true;
        if (attempted_ || !initialized_ || !window_) return false;
        attempted_ = true;
        ComPtr<ISystemMediaTransportControlsInterop> interop;
        WinString className(QStringLiteral("Windows.Media.SystemMediaTransportControls"));
        HRESULT result = RoGetActivationFactory(className.get(), interopIid, interop.putVoid());
        if (SUCCEEDED(result))
            result = interop->GetForWindow(window_, controlsIid, controls_.putVoid());
        if (FAILED(result)) {
            qWarning() << "Windows system media controls unavailable; HRESULT" << Qt::hex << quint32(result);
            return false;
        }
        controls_->get_DisplayUpdater(updater_.put());
        controls_->QueryInterface(controls2Iid, controls2_.putVoid());
        context_ = std::make_shared<CallbackState>(callback_);
        *buttonCallback_.put() = new ButtonCallback(context_);
        result = controls_->add_ButtonPressed(buttonCallback_.get(), &buttonToken_);
        buttonsRegistered_ = SUCCEEDED(result);
        if (!buttonsRegistered_)
            qWarning() << "Windows system media button registration failed; HRESULT" << Qt::hex << quint32(result);
        if (controls2_) {
            *positionCallback_.put() = new PositionCallback(context_);
            positionRegistered_ = SUCCEEDED(controls2_->add_PlaybackPositionChangeRequested(
                                               positionCallback_.get(), &positionToken_));
            WinString timelineName(QStringLiteral("Windows.Media.SystemMediaTransportControlsTimelineProperties"));
            ComPtr<IInspectable> timelineObject;
            if (SUCCEEDED(RoActivateInstance(timelineName.get(), timelineObject.put())))
                timelineObject->QueryInterface(timelineIid, timeline_.putVoid());
        }
        return true;
    }
    void publish() {
        if (!attach()) return;
        context_->epoch.store(state_.session);
        controls_->put_IsPlayEnabled(true);
        controls_->put_IsPauseEnabled(true);
        controls_->put_IsStopEnabled(true);
        controls_->put_IsRecordEnabled(false);
        controls_->put_IsFastForwardEnabled(false);
        controls_->put_IsRewindEnabled(false);
        controls_->put_IsPreviousEnabled(state_.canPrevious);
        controls_->put_IsNextEnabled(state_.canNext);
        controls_->put_PlaybackStatus(state_.buffering ? Changing : state_.paused ? Paused : Playing);
        if (updater_ && (metadataEpoch_ != state_.session || title_ != state_.title || artist_ != state_.artist)) {
            updater_->ClearAll();
            updater_->put_Type(Video);
            ComPtr<IVideoDisplayProperties> video;
            if (SUCCEEDED(updater_->get_VideoProperties(video.put()))) {
                WinString title(state_.title), artist(state_.artist);
                video->put_Title(title.get());
                video->put_Subtitle(artist.get());
            }
            updater_->Update();
            metadataEpoch_ = state_.session;
            title_ = state_.title;
            artist_ = state_.artist;
        }
        if (controls2_) {
            controls2_->put_PlaybackRate(state_.rate);
            if (timeline_) {
                const double duration = std::max(0.0, state_.duration);
                const double position = std::clamp(state_.position, 0.0, duration);
                timeline_->put_StartTime(toTimeSpan(0));
                timeline_->put_EndTime(toTimeSpan(duration));
                timeline_->put_MinSeekTime(toTimeSpan(state_.canSeek ? 0 : position));
                timeline_->put_MaxSeekTime(toTimeSpan(state_.canSeek ? duration : position));
                timeline_->put_Position(toTimeSpan(position));
                controls2_->UpdateTimelineProperties(timeline_.get());
            }
        }
        controls_->put_IsEnabled(true);
    }
    void reset() {
        resetError_.clear();
        auto checked = [this](HRESULT result) {
            if (FAILED(result) && resetError_.isEmpty())
                resetError_ = QStringLiteral("SMTC reset failed: HRESULT 0x%1")
                    .arg(quint32(result), 8, 16, QLatin1Char('0'));
        };
        if (context_) context_->epoch.store(0);
        if (controls_) {
            checked(controls_->put_IsEnabled(false));
            checked(controls_->put_PlaybackStatus(Closed));
            if (buttonsRegistered_) checked(controls_->remove_ButtonPressed(buttonToken_));
        }
        if (controls2_ && positionRegistered_)
            checked(controls2_->remove_PlaybackPositionChangeRequested(positionToken_));
        if (updater_) {
            checked(updater_->ClearAll());
            checked(updater_->Update());
        }
        buttonsRegistered_ = false;
        positionRegistered_ = false;
        // Detached handlers keep their own disabled context. A late event from
        // an old window cannot acquire a newly opened playback session.
        context_.reset();
        buttonCallback_.reset();
        positionCallback_.reset();
        timeline_.reset();
        updater_.reset();
        controls2_.reset();
        controls_.reset();
        metadataEpoch_ = 0;
        title_.clear();
        artist_.clear();
        attempted_ = false;
    }
    SystemMediaCallback callback_;
    std::shared_ptr<CallbackState> context_;
    SystemMediaState state_;
    HWND window_ = nullptr;
    bool initialized_ = false;
    bool uninitialize_ = false;
    bool attempted_ = false;
    bool buttonsRegistered_ = false;
    bool positionRegistered_ = false;
    quint64 metadataEpoch_ = 0;
    QString title_, artist_;
    QString resetError_;
    EventRegistrationToken buttonToken_{}, positionToken_{};
    ComPtr<ISystemMediaTransportControls> controls_;
    ComPtr<ISystemMediaTransportControls2> controls2_;
    ComPtr<ISystemMediaTransportControlsDisplayUpdater> updater_;
    ComPtr<ISystemMediaTransportControlsTimelineProperties> timeline_;
    ComPtr<ButtonHandler> buttonCallback_;
    ComPtr<PositionHandler> positionCallback_;
};
} // namespace

std::unique_ptr<SystemMediaBackend> createSystemMediaBackend(SystemMediaCallback callback) {
    return std::make_unique<WindowsMediaBackend>(std::move(callback));
}

QString verifyWindowsSystemMediaBackend() {
    // A normal hidden HWND supports GetForWindow; HWND_MESSAGE does not.
    // No account, artwork, user database or persistent state is involved.
    const HWND window = CreateWindowExW(0, L"STATIC", L"BBHouse offline SMTC verification",
        WS_OVERLAPPED, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return QStringLiteral("SMTC fixture window creation failed");
    struct WindowScope { HWND handle; ~WindowScope() { DestroyWindow(handle); } } scope{window};
    WindowsMediaBackend backend([](SystemMediaCommand, double, quint64) {});
    backend.setWindow(reinterpret_cast<quintptr>(window));
    SystemMediaState state;
    state.active = true;
    state.session = 1;
    state.title = QStringLiteral("BBHouse offline fixture");
    state.artist = QStringLiteral("Deployment verification");
    state.duration = 60;
    state.position = 5;
    state.canSeek = true;
    state.canNext = true;
    auto verify = [&] {
        backend.update(state);
        return backend.verificationError();
    };
    QString error = verify();
    if (!error.isEmpty()) return error;
    state.paused = false;
    state.position = 10;
    state.rate = 1.5;
    state.canPrevious = true;
    error = verify();
    if (!error.isEmpty()) return error;
    state.buffering = true;
    state.canSeek = false;
    error = verify();
    if (!error.isEmpty()) return error;
    backend.update(SystemMediaState{});
    error = backend.verificationError();
    if (!error.isEmpty()) return error;
    // Reattach catches stale registrations and objects after reset.
    ++state.session;
    state.buffering = false;
    state.title = QStringLiteral("BBHouse second offline fixture");
    error = verify();
    if (!error.isEmpty()) return error;
    backend.update(SystemMediaState{});
    return backend.verificationError();
}
