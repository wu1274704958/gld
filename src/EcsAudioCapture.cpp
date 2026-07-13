#include <ecs\audio\AudioCaptureSystem.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(PF_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

#if defined(PF_WIN32) && __has_include(<audioclientactivationparams.h>)
#define GLD_HAS_WASAPI_PROCESS_LOOPBACK 1
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <objidl.h>
#include <propidl.h>
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif
#else
#define GLD_HAS_WASAPI_PROCESS_LOOPBACK 0
#endif

namespace gld::ecs {

    namespace {
        struct ProcessMatch {
            std::uint32_t pid = 0;
            std::string name;
        };

        std::string lower_ascii(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

#if defined(PF_WIN32)
        std::string wide_to_utf8(const wchar_t* text) {
            if (!text || !*text) return {};
            const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (len <= 1) return {};
            std::string out(static_cast<std::size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
            return out;
        }

        std::optional<ProcessMatch> find_process_by_name(const std::string& process_name) {
            if (process_name.empty()) return std::nullopt;

            const std::string wanted = lower_ascii(process_name);
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            std::optional<ProcessMatch> match;
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    std::string exe = wide_to_utf8(entry.szExeFile);
                    if (lower_ascii(exe) == wanted) {
                        match = ProcessMatch{ entry.th32ProcessID, std::move(exe) };
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }

            CloseHandle(snapshot);
            return match;
        }
#else
        std::optional<ProcessMatch> find_process_by_name(const std::string&) {
            return std::nullopt;
        }
#endif

        std::string hresult_text(const char* context, long hr) {
            if (hr == E_ILLEGAL_METHOD_CALL) {
                char buf[256]{};
                std::snprintf(buf, sizeof(buf),
                    "%s failed: 0x%08lX (E_ILLEGAL_METHOD_CALL: check COM apartment and agile completion handler)",
                    context,
                    static_cast<unsigned long>(hr));
                return buf;
            }
            if (hr == E_NOTIMPL) {
                char buf[220]{};
                std::snprintf(buf, sizeof(buf),
                    "%s failed: 0x%08lX (E_NOTIMPL: method is not implemented by this audio client)",
                    context,
                    static_cast<unsigned long>(hr));
                return buf;
            }
            char buf[160]{};
            std::snprintf(buf, sizeof(buf), "%s failed: 0x%08lX", context, static_cast<unsigned long>(hr));
            return buf;
        }

        struct CaptureRuntimeState {
            ~CaptureRuntimeState() { stop(); }

            void start(std::uint32_t pid, std::string name, bool include_tree, float max_buffer_seconds) {
                if (is_running(pid, include_tree)) return;
                if (is_retry_delayed(pid, include_tree)) return;

                stop();

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    process_id = pid;
                    process_name = std::move(name);
                    include_process_tree = include_tree;
                    max_seconds = std::clamp(max_buffer_seconds, 0.1f, 30.f);
                    sample_rate = 0;
                    channels = 0;
                    sequence = 0;
                    samples.clear();
                    error.clear();
                    running = true;
                }

                stop_requested = false;
                worker = std::thread([this] { capture_loop(); });
            }

            void stop() {
                stop_requested = true;
                if (worker.joinable())
                    worker.join();
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
                process_id = 0;
                process_name.clear();
                samples.clear();
            }

            bool snapshot(PcmAudio& out, std::uint64_t last_sequence) {
                std::lock_guard<std::mutex> lock(mutex);
                if (sequence == last_sequence || samples.empty() || sample_rate == 0 || channels == 0)
                    return false;
                out.process_id = process_id;
                out.process_name = process_name;
                out.sample_rate = sample_rate;
                out.channels = channels;
                out.sequence = sequence;
                out.samples = samples;
                return true;
            }

            std::string last_error() const {
                std::lock_guard<std::mutex> lock(mutex);
                return error;
            }

        private:
            bool is_running(std::uint32_t pid, bool include_tree) const {
                std::lock_guard<std::mutex> lock(mutex);
                return running && process_id == pid && include_process_tree == include_tree;
            }

            bool is_retry_delayed(std::uint32_t pid, bool include_tree) const {
                std::lock_guard<std::mutex> lock(mutex);
                return !running &&
                    process_id == pid &&
                    include_process_tree == include_tree &&
                    !error.empty() &&
                    std::chrono::steady_clock::now() < next_retry;
            }

            void set_error(std::string message) {
                std::lock_guard<std::mutex> lock(mutex);
                error = std::move(message);
                next_retry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }

            void append_samples(std::uint32_t rate, std::uint16_t channel_count, const std::vector<float>& input) {
                if (input.empty() || rate == 0 || channel_count == 0) return;

                std::lock_guard<std::mutex> lock(mutex);
                sample_rate = rate;
                channels = channel_count;
                samples.insert(samples.end(), input.begin(), input.end());

                const std::size_t max_samples =
                    static_cast<std::size_t>(static_cast<float>(rate) * max_seconds) * channel_count;
                if (max_samples > 0 && samples.size() > max_samples)
                    samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - max_samples));
                ++sequence;
            }

            void mark_stopped() {
                std::lock_guard<std::mutex> lock(mutex);
                running = false;
            }

            void capture_loop();

            mutable std::mutex mutex;
            std::thread worker;
            std::atomic<bool> stop_requested{ false };
            bool running = false;
            bool include_process_tree = true;
            float max_seconds = 2.f;
            std::uint32_t process_id = 0;
            std::string process_name;
            std::uint32_t sample_rate = 0;
            std::uint16_t channels = 0;
            std::uint64_t sequence = 0;
            std::vector<float> samples;
            std::string error;
            std::chrono::steady_clock::time_point next_retry{};
        };

#if GLD_HAS_WASAPI_PROCESS_LOOPBACK
        class ActivateCompletionHandler final :
            public IActivateAudioInterfaceCompletionHandler,
            public IAgileObject {
        public:
            explicit ActivateCompletionHandler(HANDLE completed_event) : completed_event_(completed_event) {
                CoCreateFreeThreadedMarshaler(
                    static_cast<IUnknown*>(static_cast<IActivateAudioInterfaceCompletionHandler*>(this)),
                    &free_threaded_marshaler_);
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
                if (!object) return E_POINTER;
                if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
                    *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
                    AddRef();
                    return S_OK;
                }
                if (riid == __uuidof(IAgileObject)) {
                    *object = static_cast<IAgileObject*>(this);
                    AddRef();
                    return S_OK;
                }
                if (riid == __uuidof(IMarshal) && free_threaded_marshaler_)
                    return free_threaded_marshaler_->QueryInterface(riid, object);
                *object = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override {
                return static_cast<ULONG>(InterlockedIncrement(&refs_));
            }

            ULONG STDMETHODCALLTYPE Release() override {
                const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&refs_));
                if (refs == 0) delete this;
                return refs;
            }

            HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
                IUnknown* audio_interface = nullptr;
                HRESULT activate_result = E_FAIL;
                HRESULT hr = operation ? operation->GetActivateResult(&activate_result, &audio_interface) : E_POINTER;
                result = FAILED(hr) ? hr : activate_result;
                if (SUCCEEDED(result) && audio_interface)
                    result = audio_interface->QueryInterface(IID_PPV_ARGS(&client));
                if (audio_interface) audio_interface->Release();
                SetEvent(completed_event_);
                return S_OK;
            }

            HRESULT result = E_PENDING;
            IAudioClient* client = nullptr;

        private:
            ~ActivateCompletionHandler() {
                if (free_threaded_marshaler_) {
                    free_threaded_marshaler_->Release();
                    free_threaded_marshaler_ = nullptr;
                }
            }

            HANDLE completed_event_ = nullptr;
            IUnknown* free_threaded_marshaler_ = nullptr;
            volatile long refs_ = 1;
        };

        IAudioClient* activate_process_loopback(std::uint32_t pid, bool include_tree, std::string& error) {
            HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!completed) {
                error = "CreateEventW failed for WASAPI activation";
                return nullptr;
            }

            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            params.ProcessLoopbackParams.ProcessLoopbackMode = include_tree
                ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE
                : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
            params.ProcessLoopbackParams.TargetProcessId = pid;

            PROPVARIANT prop{};
            prop.vt = VT_BLOB;
            prop.blob.cbSize = sizeof(params);
            prop.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

            auto* handler = new ActivateCompletionHandler(completed);
            IActivateAudioInterfaceAsyncOperation* operation = nullptr;
            HRESULT hr = ActivateAudioInterfaceAsync(
                VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                __uuidof(IAudioClient),
                &prop,
                handler,
                &operation);

            if (SUCCEEDED(hr)) {
                WaitForSingleObject(completed, INFINITE);
                hr = handler->result;
            }

            IAudioClient* client = nullptr;
            if (SUCCEEDED(hr)) {
                client = handler->client;
                handler->client = nullptr;
            } else {
                error = hresult_text("ActivateAudioInterfaceAsync", hr);
            }

            if (operation) operation->Release();
            handler->Release();
            CloseHandle(completed);
            return client;
        }

        bool is_extensible_subformat(const WAVEFORMATEX* fmt, const GUID& subformat) {
            if (!fmt || fmt->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
                fmt->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                return false;
            }
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
            return IsEqualGUID(ext->SubFormat, subformat);
        }

        bool is_float_format(const WAVEFORMATEX* fmt) {
            return fmt && (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                is_extensible_subformat(fmt, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
        }

        bool is_pcm_format(const WAVEFORMATEX* fmt) {
            return fmt && (fmt->wFormatTag == WAVE_FORMAT_PCM ||
                is_extensible_subformat(fmt, KSDATAFORMAT_SUBTYPE_PCM));
        }

        float read_pcm_sample(const BYTE* src, WORD bits_per_sample, bool is_float) {
            if (is_float && bits_per_sample == 32) {
                float v = 0.f;
                std::memcpy(&v, src, sizeof(float));
                return v;
            }

            switch (bits_per_sample) {
            case 8:
                return (static_cast<int>(*src) - 128) / 128.f;
            case 16: {
                std::int16_t v = 0;
                std::memcpy(&v, src, sizeof(v));
                return static_cast<float>(v) / 32768.f;
            }
            case 24: {
                std::int32_t v = static_cast<std::int32_t>(src[0]) |
                    (static_cast<std::int32_t>(src[1]) << 8) |
                    (static_cast<std::int32_t>(src[2]) << 16);
                if (v & 0x00800000) v |= static_cast<std::int32_t>(0xFF000000);
                return static_cast<float>(v) / 8388608.f;
            }
            case 32: {
                std::int32_t v = 0;
                std::memcpy(&v, src, sizeof(v));
                return static_cast<float>(v) / 2147483648.f;
            }
            default:
                return 0.f;
            }
        }

        bool convert_packet_to_float(
            const BYTE* data,
            UINT32 frames,
            DWORD flags,
            const WAVEFORMATEX* fmt,
            std::vector<float>& out) {

            if (!fmt || fmt->nChannels == 0 || fmt->nBlockAlign == 0) return false;
            const bool float_format = is_float_format(fmt);
            const bool pcm_format = is_pcm_format(fmt);
            if (!float_format && !pcm_format) return false;

            const WORD bytes_per_sample = fmt->wBitsPerSample / 8;
            if (bytes_per_sample == 0) return false;

            out.resize(static_cast<std::size_t>(frames) * fmt->nChannels);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(out.begin(), out.end(), 0.f);
                return true;
            }

            for (UINT32 frame = 0; frame < frames; ++frame) {
                const BYTE* frame_data = data + static_cast<std::size_t>(frame) * fmt->nBlockAlign;
                for (WORD ch = 0; ch < fmt->nChannels; ++ch) {
                    const BYTE* sample = frame_data + static_cast<std::size_t>(ch) * bytes_per_sample;
                    out[static_cast<std::size_t>(frame) * fmt->nChannels + ch] =
                        read_pcm_sample(sample, fmt->wBitsPerSample, float_format);
                }
            }
            return true;
        }
#endif

        std::shared_ptr<CaptureRuntimeState> ensure_state(AudioCaptureRuntime& runtime) {
            auto state = std::static_pointer_cast<CaptureRuntimeState>(runtime.state);
            if (!state) {
                state = std::make_shared<CaptureRuntimeState>();
                runtime.state = state;
            }
            return state;
        }

        void destroy_output_entity(EcsWorld& w, AudioCaptureRuntime& runtime) {
            if (runtime.output_entity != entt::null && w.reg().valid(runtime.output_entity))
                w.reg().destroy(runtime.output_entity);
            runtime.output_entity = entt::null;
            runtime.active_process_id = 0;
            runtime.active_process_name.clear();
            runtime.last_sequence = 0;
            runtime.last_publish_time = {};
        }
    }

    void CaptureRuntimeState::capture_loop() {
#if GLD_HAS_WASAPI_PROCESS_LOOPBACK
        const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool co_initialized = SUCCEEDED(co_hr);
        if (FAILED(co_hr) && co_hr != RPC_E_CHANGED_MODE) {
            set_error(hresult_text("CoInitializeEx", co_hr));
            mark_stopped();
            return;
        }

        std::uint32_t pid = 0;
        bool include_tree = true;
        {
            std::lock_guard<std::mutex> lock(mutex);
            pid = process_id;
            include_tree = include_process_tree;
        }

        std::string activation_error;
        IAudioClient* audio_client = activate_process_loopback(pid, include_tree, activation_error);
        if (!audio_client) {
            set_error(activation_error.empty() ? "WASAPI process loopback activation failed" : activation_error);
            if (co_initialized) CoUninitialize();
            mark_stopped();
            return;
        }

        WAVEFORMATEX* mix_format = nullptr;
        WAVEFORMATEX fallback_format{};
        WAVEFORMATEX* capture_format = nullptr;
        IAudioCaptureClient* capture_client = nullptr;
        HRESULT hr = audio_client->GetMixFormat(&mix_format);
        if (hr == E_NOTIMPL) {
            fallback_format.wFormatTag = WAVE_FORMAT_PCM;
            fallback_format.nChannels = 2;
            fallback_format.nSamplesPerSec = 44100;
            fallback_format.wBitsPerSample = 16;
            fallback_format.nBlockAlign = fallback_format.nChannels * fallback_format.wBitsPerSample / 8;
            fallback_format.nAvgBytesPerSec = fallback_format.nSamplesPerSec * fallback_format.nBlockAlign;
            fallback_format.cbSize = 0;
            capture_format = &fallback_format;
            hr = S_OK;
        } else if (SUCCEEDED(hr)) {
            capture_format = mix_format;
        }

        if (FAILED(hr)) {
            set_error(hresult_text("IAudioClient::GetMixFormat", hr));
            if (mix_format) CoTaskMemFree(mix_format);
            audio_client->Release();
            if (co_initialized) CoUninitialize();
            mark_stopped();
            return;
        }

        hr = audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            0,
            0,
            capture_format,
            nullptr);
        if (FAILED(hr)) {
            set_error(hresult_text("IAudioClient::Initialize", hr));
            if (mix_format) CoTaskMemFree(mix_format);
            audio_client->Release();
            if (co_initialized) CoUninitialize();
            mark_stopped();
            return;
        }

        hr = audio_client->GetService(IID_PPV_ARGS(&capture_client));
        if (FAILED(hr)) {
            set_error(hresult_text("IAudioClient::GetService(IAudioCaptureClient)", hr));
            if (mix_format) CoTaskMemFree(mix_format);
            audio_client->Release();
            if (co_initialized) CoUninitialize();
            mark_stopped();
            return;
        }

        hr = audio_client->Start();
        if (FAILED(hr)) {
            set_error(hresult_text("IAudioClient::Start", hr));
            capture_client->Release();
            if (mix_format) CoTaskMemFree(mix_format);
            audio_client->Release();
            if (co_initialized) CoUninitialize();
            mark_stopped();
            return;
        }

        while (!stop_requested) {
            UINT32 packet_frames = 0;
            hr = capture_client->GetNextPacketSize(&packet_frames);
            if (FAILED(hr)) {
                set_error(hresult_text("GetNextPacketSize", hr));
                break;
            }

            if (packet_frames == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            while (packet_frames > 0 && !stop_requested) {
                BYTE* data = nullptr;
                DWORD flags = 0;
                UINT64 device_position = 0;
                UINT64 qpc_position = 0;
                UINT32 frames_available = 0;
                hr = capture_client->GetBuffer(&data, &frames_available, &flags, &device_position, &qpc_position);
                if (FAILED(hr)) {
                    set_error(hresult_text("GetBuffer", hr));
                    break;
                }

                std::vector<float> converted;
                if (convert_packet_to_float(data, frames_available, flags, capture_format, converted))
                    append_samples(capture_format->nSamplesPerSec, capture_format->nChannels, converted);
                else
                    set_error("Unsupported WASAPI capture format");

                capture_client->ReleaseBuffer(frames_available);

                hr = capture_client->GetNextPacketSize(&packet_frames);
                if (FAILED(hr)) {
                    set_error(hresult_text("GetNextPacketSize", hr));
                    break;
                }
            }
        }

        audio_client->Stop();
        capture_client->Release();
        CoTaskMemFree(mix_format);
        audio_client->Release();
        if (co_initialized) CoUninitialize();
        mark_stopped();
#else
        set_error("WASAPI process loopback is unavailable with this Windows SDK/platform");
        mark_stopped();
#endif
    }

    void audio_capture_system(EcsWorld& w) {
        auto& runtime = w.resource_or_add<AudioCaptureRuntime>();
        auto state = ensure_state(runtime);
        auto* request = w.try_resource<AudioCaptureRequest>();
        if (!request || request->process_name.empty()) {
            state->stop();
            destroy_output_entity(w, runtime);
            return;
        }

        auto match = find_process_by_name(request->process_name);
        if (!match) {
            state->stop();
            destroy_output_entity(w, runtime);
            runtime.last_error.clear();
            return;
        }

        state->start(match->pid, match->name, request->include_process_tree, request->max_buffer_seconds);
        runtime.active_process_id = match->pid;
        runtime.active_process_name = match->name;

        const float publish_hz = request->publish_hz;
        if (publish_hz > 0.f && runtime.last_sequence != 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto period = std::chrono::duration<float>(1.f / publish_hz);
            if (runtime.last_publish_time.time_since_epoch().count() != 0 &&
                now - runtime.last_publish_time < period) {
                runtime.last_error = state->last_error();
                return;
            }
        }

        PcmAudio pcm;
        if (!state->snapshot(pcm, runtime.last_sequence)) {
            runtime.last_error = state->last_error();
            return;
        }

        if (runtime.output_entity == entt::null || !w.reg().valid(runtime.output_entity))
            runtime.output_entity = w.spawn();

        runtime.last_sequence = pcm.sequence;
        runtime.last_publish_time = std::chrono::steady_clock::now();
        runtime.last_error.clear();
        w.reg().emplace_or_replace<PcmAudio>(runtime.output_entity, std::move(pcm));
    }

    void AudioCapturePlugin(App& app) {
        app.world.resource_or_add<AudioCaptureRuntime>();
        app.add_system(Stage::Update, audio_capture_system);
    }
}
