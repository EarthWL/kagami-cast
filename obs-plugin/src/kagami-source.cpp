// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Kagami Cast" OBS source: shows the iPhone/iPad picture that Kagami Cast's
// uxplay sends through d3d11ipcsink (see kagami-ipc.hpp).
//
// worker thread   connects to the pipe (retrying), reads CONFIG and pulls frames.
//                 Same adapter as OBS: hands the newest frame's NT handle to the
//                 graphics thread (zero copy). Other adapter: opens the texture on
//                 a helper device for that adapter and copies it to memory.
// graphics thread opens the handle with gs_texture_open_nt_shared and draws it
//                 under the keyed mutex, or uploads the copied pixels.
// At most two server textures are held (the one on screen and the newest one);
// replaced ones are given back with RELEASE_DATA by the worker.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <obs-module.h>
#include <plugin-support.h>

#include "kagami-ipc.hpp"
#include "kagami-source.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char *kDefaultPipe = "\\\\.\\pipe\\kagami-cast";
constexpr const char *kSettingPipe = "pipe";

template<typename T> void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

std::wstring Utf8ToWide(const char *s)
{
	if (!s || !*s)
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
	return w;
}

std::string WideToUtf8(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n - 1 : 0, '\0');
	if (n > 1)
		WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
	return s;
}

int64_t LuidToInt64(const LUID &l)
{
	return (static_cast<int64_t>(l.HighPart) << 32) | l.LowPart;
}

// The adapter OBS renders on. Kagami Cast reads it (obs-adapter.txt) to send its
// frames from the same adapter, which is what makes the zero-copy path possible.
bool QueryObsAdapter(int64_t &luid, std::string &name)
{
	bool ok = false;
	obs_enter_graphics();
	auto *device = static_cast<ID3D11Device *>(gs_get_device_obj());
	IDXGIDevice *dxgi = nullptr;
	IDXGIAdapter *adapter = nullptr;
	if (device && SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi))) &&
	    SUCCEEDED(dxgi->GetAdapter(&adapter))) {
		DXGI_ADAPTER_DESC desc;
		if (SUCCEEDED(adapter->GetDesc(&desc))) {
			luid = LuidToInt64(desc.AdapterLuid);
			name = WideToUtf8(desc.Description);
			ok = true;
		}
	}
	SafeRelease(adapter);
	SafeRelease(dxgi);
	obs_leave_graphics();
	return ok;
}

void PublishObsAdapter(int64_t luid, const std::string &name)
{
	wchar_t base[MAX_PATH];
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return;
	std::wstring dir = std::wstring(base) + L"\\KagamiCast";
	CreateDirectoryW(dir.c_str(), nullptr);
	FILE *f = _wfopen((dir + L"\\obs-adapter.txt").c_str(), L"wb");
	if (!f)
		return;
	fprintf(f, "adapter_luid=%lld\r\nadapter_name=%s\r\n", static_cast<long long>(luid), name.c_str());
	fclose(f);
}

// Helper device on a given adapter, for the CPU-copy fallback.
ID3D11Device *CreateDeviceForLuid(int64_t luid, ID3D11DeviceContext **context)
{
	IDXGIFactory1 *factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
		return nullptr;
	ID3D11Device *device = nullptr;
	IDXGIAdapter1 *adapter = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);
		if (LuidToInt64(desc.AdapterLuid) == luid)
			D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
					  nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, context);
		SafeRelease(adapter);
		if (device)
			break;
	}
	SafeRelease(factory);
	return device;
}

enum class Mode { Waiting, Gpu, CpuCopy, Error };

class KagamiSource {
public:
	KagamiSource(obs_data_t *settings, obs_source_t *source) : source_(source)
	{
		if (QueryObsAdapter(obs_luid_, obs_adapter_name_)) {
			PublishObsAdapter(obs_luid_, obs_adapter_name_);
			obs_log(LOG_INFO, "OBS renders on \"%s\" (LUID %lld)", obs_adapter_name_.c_str(),
				static_cast<long long>(obs_luid_));
		}
		Update(settings);
	}

	~KagamiSource()
	{
		StopWorker();
		obs_enter_graphics();
		if (tex_)
			gs_texture_destroy(tex_);
		obs_leave_graphics();
		if (pending_.handle)
			CloseHandle(pending_.handle);
	}

	void Update(obs_data_t *settings)
	{
		const char *pipe = obs_data_get_string(settings, kSettingPipe);
		std::wstring wpipe = Utf8ToWide(pipe && *pipe ? pipe : kDefaultPipe);
		if (wpipe == pipe_ && worker_.joinable())
			return;
		StopWorker();
		pipe_ = wpipe;
		stop_ = false;
		worker_ = std::thread([this] { Run(); });
	}

	uint32_t Width() { return width_; }
	uint32_t Height() { return height_; }

	std::string Status()
	{
		std::lock_guard<std::mutex> lk(mutex_);
		char buf[512];
		switch (mode_) {
		case Mode::Gpu:
			snprintf(buf, sizeof(buf), obs_module_text("KagamiCast.Status.Receiving"), (int)width_,
				 (int)height_, obs_adapter_name_.c_str());
			return buf;
		case Mode::CpuCopy:
			snprintf(buf, sizeof(buf), obs_module_text("KagamiCast.Status.Mismatch"), (int)width_,
				 (int)height_);
			return buf;
		case Mode::Error:
			snprintf(buf, sizeof(buf), obs_module_text("KagamiCast.Status.Error"), error_.c_str());
			return buf;
		default:
			return obs_module_text("KagamiCast.Status.Waiting");
		}
	}

	// graphics thread
	void Render(gs_effect_t *effect)
	{
		Pending next;
		bool clear = false, have_cpu = false;
		{
			std::lock_guard<std::mutex> lk(mutex_);
			clear = clear_;
			clear_ = false;
			next = pending_;
			pending_ = {};
			if (cpu_fresh_) {
				render_pixels_.swap(cpu_pixels_);
				render_w_ = cpu_w_;
				render_h_ = cpu_h_;
				cpu_fresh_ = false;
				have_cpu = true;
			}
		}
		if (clear)
			DropTexture();

		if (next.handle) {
			gs_texture_t *t =
				gs_texture_open_nt_shared(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(next.handle)));
			CloseHandle(next.handle);
			if (t) {
				DropTexture();
				tex_ = t;
				tex_server_handle_ = next.server_handle;
				tex_session_ = next.session;
				tex_dynamic_ = false;
			} else {
				QueueRelease(next.server_handle, next.session);
			}
		}
		if (have_cpu) {
			if (!tex_ || !tex_dynamic_ || gs_texture_get_width(tex_) != render_w_ ||
			    gs_texture_get_height(tex_) != render_h_) {
				DropTexture();
				tex_ = gs_texture_create(render_w_, render_h_, GS_BGRA, 1, nullptr, GS_DYNAMIC);
				tex_dynamic_ = true;
			}
			if (tex_)
				gs_texture_set_image(tex_, render_pixels_.data(), render_w_ * 4, false);
		}
		if (!tex_)
			return;

		if (!tex_dynamic_ && gs_texture_acquire_sync(tex_, 0, 50) != 0)
			return; // the sender still writes it; keep the previous output
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, tex_);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(tex_, 0, 0, 0);
		if (!tex_dynamic_)
			gs_texture_release_sync(tex_, 0);
	}

private:
	struct Pending {
		HANDLE handle = nullptr;        // our duplicate, opened by the graphics thread
		HANDLE server_handle = nullptr; // the sender's value, given back with RELEASE_DATA
		uint64_t session = 0;           // connection it came from
	};

	// graphics thread
	void DropTexture()
	{
		if (tex_) {
			gs_texture_destroy(tex_);
			tex_ = nullptr;
		}
		if (tex_server_handle_) {
			QueueRelease(tex_server_handle_, tex_session_);
			tex_server_handle_ = nullptr;
		}
	}

	// Handles of an ended connection mean nothing to the next sender, so only
	// those of the live one are given back.
	void QueueRelease(HANDLE server_handle, uint64_t session)
	{
		std::lock_guard<std::mutex> lk(mutex_);
		if (session == session_)
			to_release_.push_back(server_handle);
	}

	void SetMode(Mode mode, const std::string &error = {})
	{
		std::lock_guard<std::mutex> lk(mutex_);
		mode_ = mode;
		error_ = error;
	}

	void StopWorker()
	{
		if (!worker_.joinable())
			return;
		{
			std::lock_guard<std::mutex> lk(mutex_);
			stop_ = true;
			if (client_)
				client_->Stop();
		}
		wake_.notify_all();
		worker_.join();
	}

	bool SleepFor(std::chrono::milliseconds ms)
	{
		std::unique_lock<std::mutex> lk(mutex_);
		wake_.wait_for(lk, ms, [this] { return stop_.load(); });
		return !stop_;
	}

	// worker thread
	void Run()
	{
		std::string last_error;
		while (!stop_) {
			kagami::IpcClient client;
			const bool connected = client.Connect(pipe_); // non-blocking; resets the stop event
			{
				// Registered only after Connect, so a Stop() from StopWorker cannot be
				// wiped out by Connect's reset.
				std::lock_guard<std::mutex> lk(mutex_);
				if (stop_)
					break;
				client_ = &client;
			}
			if (!connected) {
				SetMode(Mode::Waiting);
			} else {
				Session(client, last_error);
			}
			{
				std::lock_guard<std::mutex> lk(mutex_);
				client_ = nullptr;
			}
			SleepFor(std::chrono::milliseconds(500));
		}
	}

	void Session(kagami::IpcClient &client, std::string &last_error)
	{
		kagami::IpcConfig cfg;
		if (client.ReadConfig(cfg) != kagami::IpcResult::Ok) {
			client.Close(false);
			SetMode(Mode::Waiting);
			return;
		}
		int w = 0, h = 0;
		kagami::CapsGetInt(cfg.caps, "width", w);
		kagami::CapsGetInt(cfg.caps, "height", h);
		const std::string format = kagami::CapsGetString(cfg.caps, "format");
		if (format != "BGRA" || w <= 0 || h <= 0) {
			SetMode(Mode::Error, "unsupported format " + format + " (Kagami Cast must send BGRA)");
			client.Close(true);
			return;
		}
		const bool gpu = cfg.adapter_luid == obs_luid_;
		obs_log(LOG_INFO, "connected: %dx%d %s, sender adapter LUID %lld -> %s", w, h, format.c_str(),
			static_cast<long long>(cfg.adapter_luid), gpu ? "zero copy" : "CPU copy (adapter differs from OBS)");

		ID3D11DeviceContext *ctx = nullptr;
		ID3D11Device *dev = gpu ? nullptr : CreateDeviceForLuid(cfg.adapter_luid, &ctx);
		ID3D11Device1 *dev1 = nullptr;
		if (!gpu && (!dev || FAILED(dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&dev1))))) {
			SetMode(Mode::Error, "cannot open the sender's graphics adapter");
			SafeRelease(ctx);
			SafeRelease(dev);
			client.Close(true);
			return;
		}
		ID3D11Texture2D *staging = nullptr;
		{
			std::lock_guard<std::mutex> lk(mutex_);
			session_++;
			width_ = static_cast<uint32_t>(w);
			height_ = static_cast<uint32_t>(h);
			mode_ = gpu ? Mode::Gpu : Mode::CpuCopy;
			error_.clear();
		}

		bool eos = false;
		for (;;) {
			std::vector<HANDLE> releases;
			{
				std::lock_guard<std::mutex> lk(mutex_);
				releases.swap(to_release_);
			}
			for (HANDLE r : releases)
				client.Release(r);

			kagami::IpcFrame fr;
			kagami::IpcResult res = client.NextFrame(fr);
			if (res != kagami::IpcResult::Ok) {
				eos = res == kagami::IpcResult::Eos;
				if (res == kagami::IpcResult::Error && client.LastError() != last_error) {
					last_error = client.LastError();
					obs_log(LOG_WARNING, "connection ended: %s", last_error.c_str());
				}
				break;
			}
			if (!fr.caps.empty()) {
				kagami::CapsGetInt(fr.caps, "width", w);
				kagami::CapsGetInt(fr.caps, "height", h);
				std::lock_guard<std::mutex> lk(mutex_);
				width_ = static_cast<uint32_t>(w);
				height_ = static_cast<uint32_t>(h);
				SafeRelease(staging);
			}

			if (gpu) {
				HANDLE replaced_handle = nullptr, replaced_server = nullptr;
				{
					std::lock_guard<std::mutex> lk(mutex_);
					replaced_handle = pending_.handle;
					replaced_server = pending_.server_handle;
					pending_.handle = fr.handle;
					pending_.server_handle = fr.server_handle;
					pending_.session = session_;
				}
				if (replaced_handle) { // OBS did not draw it in time
					CloseHandle(replaced_handle);
					client.Release(replaced_server);
				}
				continue;
			}

			// CPU copy on the sender's adapter
			ID3D11Texture2D *tex = nullptr;
			HRESULT hr = dev1->OpenSharedResource1(fr.handle, __uuidof(ID3D11Texture2D),
							       reinterpret_cast<void **>(&tex));
			CloseHandle(fr.handle);
			if (SUCCEEDED(hr)) {
				IDXGIKeyedMutex *km = nullptr;
				tex->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&km));
				if (!staging) {
					D3D11_TEXTURE2D_DESC desc;
					tex->GetDesc(&desc);
					desc.Usage = D3D11_USAGE_STAGING;
					desc.BindFlags = 0;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					desc.MiscFlags = 0;
					dev->CreateTexture2D(&desc, nullptr, &staging);
				}
				if (km && staging && km->AcquireSync(0, 100) == S_OK) {
					ctx->CopyResource(staging, tex);
					km->ReleaseSync(0);
					D3D11_MAPPED_SUBRESOURCE m;
					if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
						std::lock_guard<std::mutex> lk(mutex_);
						cpu_w_ = static_cast<uint32_t>(w);
						cpu_h_ = static_cast<uint32_t>(h);
						cpu_pixels_.resize(static_cast<size_t>(cpu_w_) * cpu_h_ * 4);
						for (uint32_t y = 0; y < cpu_h_; y++)
							memcpy(cpu_pixels_.data() + static_cast<size_t>(y) * cpu_w_ * 4,
							       static_cast<uint8_t *>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
							       static_cast<size_t>(cpu_w_) * 4);
						cpu_fresh_ = true;
						ctx->Unmap(staging, 0);
					}
				}
				SafeRelease(km);
				SafeRelease(tex);
			}
			client.Release(fr.server_handle);
		}

		client.Close(eos);
		SafeRelease(staging);
		SafeRelease(dev1);
		SafeRelease(ctx);
		SafeRelease(dev);
		{
			std::lock_guard<std::mutex> lk(mutex_);
			if (pending_.handle)
				CloseHandle(pending_.handle);
			pending_ = {};
			to_release_.clear(); // the server is gone; its handles mean nothing now
			session_++;
			clear_ = true;
			width_ = height_ = 0;
			if (mode_ != Mode::Error)
				mode_ = Mode::Waiting;
		}
	}

	obs_source_t *source_;
	std::wstring pipe_;
	std::thread worker_;
	std::atomic<bool> stop_{false};
	std::condition_variable wake_;
	std::mutex mutex_;

	// shared, guarded by mutex_
	kagami::IpcClient *client_ = nullptr;
	Pending pending_;
	std::vector<HANDLE> to_release_;
	uint64_t session_ = 0; // bumped when a connection starts and when it ends
	std::vector<uint8_t> cpu_pixels_;
	uint32_t cpu_w_ = 0, cpu_h_ = 0;
	bool cpu_fresh_ = false;
	bool clear_ = false;
	Mode mode_ = Mode::Waiting;
	std::string error_;
	std::atomic<uint32_t> width_{0}, height_{0};

	int64_t obs_luid_ = 0;
	std::string obs_adapter_name_;

	// graphics thread only
	gs_texture_t *tex_ = nullptr;
	HANDLE tex_server_handle_ = nullptr;
	uint64_t tex_session_ = 0;
	bool tex_dynamic_ = false;
	std::vector<uint8_t> render_pixels_;
	uint32_t render_w_ = 0, render_h_ = 0;
};

const char *GetName(void *)
{
	return obs_module_text("KagamiCast");
}

void *Create(obs_data_t *settings, obs_source_t *source)
{
	return new KagamiSource(settings, source);
}

void Destroy(void *data)
{
	delete static_cast<KagamiSource *>(data);
}

void UpdateSettings(void *data, obs_data_t *settings)
{
	static_cast<KagamiSource *>(data)->Update(settings);
}

void GetDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kSettingPipe, kDefaultPipe);
}

obs_properties_t *GetProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	if (data) {
		const std::string status = static_cast<KagamiSource *>(data)->Status();
		obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
	}
	obs_property_t *pipe =
		obs_properties_add_text(props, kSettingPipe, obs_module_text("KagamiCast.Pipe"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(pipe, obs_module_text("KagamiCast.Pipe.Description"));
	return props;
}

void VideoRender(void *data, gs_effect_t *effect)
{
	static_cast<KagamiSource *>(data)->Render(effect);
}

uint32_t GetWidth(void *data)
{
	return static_cast<KagamiSource *>(data)->Width();
}

uint32_t GetHeight(void *data)
{
	return static_cast<KagamiSource *>(data)->Height();
}

} // namespace

void kagami_register_source(void)
{
	obs_source_info info = {};
	info.id = "kagami_cast_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	info.get_name = GetName;
	info.create = Create;
	info.destroy = Destroy;
	info.update = UpdateSettings;
	info.get_defaults = GetDefaults;
	info.get_properties = GetProperties;
	info.video_render = VideoRender;
	info.get_width = GetWidth;
	info.get_height = GetHeight;
	obs_register_source(&info);
}
