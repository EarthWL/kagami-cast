// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-alone check of kagami-ipc.cpp against a real d3d11ipcsink, without OBS.
//   ipc-client-test [--pipe NAME] [--frames N] [--adapter INDEX] [--bmp FILE]
// --adapter forces the DXGI adapter used to open the textures (default: the one
// whose LUID the server announced) to show what happens when it does not match.
#include "../src/kagami-ipc.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <chrono>
#include <cstdio>
#include <string>

using namespace kagami;

template<typename T> static void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

static int64_t LuidToInt64(const LUID &l)
{
	return (static_cast<int64_t>(l.HighPart) << 32) | l.LowPart;
}

static bool SaveBmp(const wchar_t *path, const uint8_t *bgra, int w, int h, UINT pitch)
{
	FILE *f = _wfopen(path, L"wb");
	if (!f)
		return false;
	const uint32_t row = static_cast<uint32_t>(w) * 4, img = row * h;
	uint8_t hdr[54] = {'B', 'M'};
	auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
	put32(2, 54 + img);
	put32(10, 54);
	put32(14, 40);
	put32(18, static_cast<uint32_t>(w));
	put32(22, static_cast<uint32_t>(-h)); // top-down
	hdr[26] = 1;
	hdr[28] = 32;
	put32(34, img);
	fwrite(hdr, 1, 54, f);
	for (int y = 0; y < h; y++)
		fwrite(bgra + static_cast<size_t>(y) * pitch, 1, row, f);
	fclose(f);
	return true;
}

int wmain(int argc, wchar_t **argv)
{
	std::wstring pipe = L"\\\\.\\pipe\\gst.d3d11.ipc", bmp;
	int frames = 300, force_adapter = -1;
	for (int i = 1; i + 1 < argc; i += 2) {
		std::wstring a = argv[i];
		if (a == L"--pipe")
			pipe = argv[i + 1];
		else if (a == L"--frames")
			frames = _wtoi(argv[i + 1]);
		else if (a == L"--adapter")
			force_adapter = _wtoi(argv[i + 1]);
		else if (a == L"--bmp")
			bmp = argv[i + 1];
	}

	IpcClient client;
	auto t0 = std::chrono::steady_clock::now();
	while (!client.Connect(pipe)) {
		if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(15)) {
			printf("FAIL connect: %s\n", client.LastError().c_str());
			return 1;
		}
		Sleep(100);
	}
	IpcConfig cfg;
	if (client.ReadConfig(cfg) != IpcResult::Ok) {
		printf("FAIL config: %s\n", client.LastError().c_str());
		return 1;
	}
	int w = 0, h = 0;
	CapsGetInt(cfg.caps, "width", w);
	CapsGetInt(cfg.caps, "height", h);
	const std::string fmt = CapsGetString(cfg.caps, "format");
	printf("CONFIG  server pid %lu, adapter LUID %lld, %dx%d %s\n", cfg.server_pid, (long long)cfg.adapter_luid, w,
	       h, fmt.c_str());

	// pick the adapter
	IDXGIFactory1 *factory = nullptr;
	CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory));
	IDXGIAdapter1 *adapter = nullptr, *chosen = nullptr;
	int chosen_index = -1;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 d;
		adapter->GetDesc1(&d);
		bool match = LuidToInt64(d.AdapterLuid) == cfg.adapter_luid;
		printf("  adapter %u: %ls (LUID %lld)%s\n", i, d.Description, (long long)LuidToInt64(d.AdapterLuid),
		       match ? "  <- server" : "");
		if ((force_adapter < 0 && match && !chosen) || static_cast<int>(i) == force_adapter) {
			SafeRelease(chosen);
			chosen = adapter;
			chosen->AddRef();
			chosen_index = static_cast<int>(i);
		}
		SafeRelease(adapter);
	}
	SafeRelease(factory);
	if (!chosen) {
		printf("FAIL no adapter\n");
		return 1;
	}
	printf("opening textures on adapter %d\n", chosen_index);

	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	D3D11CreateDevice(chosen, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
			  D3D11_SDK_VERSION, &dev, nullptr, &ctx);
	SafeRelease(chosen);
	ID3D11Device1 *dev1 = nullptr;
	if (!dev || FAILED(dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&dev1)))) {
		printf("FAIL device\n");
		return 1;
	}

	int ok = 0, open_fail = 0;
	HANDLE prev = nullptr;
	uint64_t first_pts = 0, last_pts = 0;
	auto start = std::chrono::steady_clock::now();
	for (int n = 0; n < frames; n++) {
		IpcFrame fr;
		IpcResult r = client.NextFrame(fr);
		if (r == IpcResult::Eos) {
			printf("EOS after %d frames\n", n);
			break;
		}
		if (r != IpcResult::Ok) {
			printf("FAIL frame %d: %s\n", n, client.LastError().c_str());
			break;
		}
		ID3D11Texture2D *tex = nullptr;
		HRESULT hr = dev1->OpenSharedResource1(fr.handle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex));
		CloseHandle(fr.handle);
		if (FAILED(hr)) {
			if (open_fail++ == 0)
				printf("OpenSharedResource1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
		} else {
			IDXGIKeyedMutex *km = nullptr;
			tex->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&km));
			if (km && km->AcquireSync(0, 1000) == S_OK) {
				if (ok == 0 && !bmp.empty()) {
					D3D11_TEXTURE2D_DESC desc;
					tex->GetDesc(&desc);
					desc.Usage = D3D11_USAGE_STAGING;
					desc.BindFlags = 0;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					desc.MiscFlags = 0;
					ID3D11Texture2D *staging = nullptr;
					dev->CreateTexture2D(&desc, nullptr, &staging);
					ctx->CopyResource(staging, tex);
					km->ReleaseSync(0);
					D3D11_MAPPED_SUBRESOURCE m;
					if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
					    SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
						printf("first frame saved: %s\n",
						       SaveBmp(bmp.c_str(), static_cast<uint8_t *>(m.pData), w, h, m.RowPitch)
							       ? "yes"
							       : "no");
						ctx->Unmap(staging, 0);
					} else {
						printf("texture format %d (no BMP)\n", desc.Format);
					}
					SafeRelease(staging);
				} else {
					km->ReleaseSync(0);
				}
				if (ok == 0)
					first_pts = fr.pts;
				last_pts = fr.pts;
				ok++;
			}
			SafeRelease(km);
			SafeRelease(tex);
		}
		if (prev)
			client.Release(prev);
		prev = fr.server_handle;
	}
	if (prev)
		client.Release(prev);
	double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	printf("RESULT frames ok %d, open failures %d, %.1f fps wall, %.1f fps by pts\n", ok, open_fail,
	       ok / (secs > 0 ? secs : 1), ok > 1 ? (ok - 1) / ((last_pts - first_pts) / 1e9) : 0.0);
	client.Close(true);
	SafeRelease(dev1);
	SafeRelease(ctx);
	SafeRelease(dev);
	return ok > 0 ? 0 : 2;
}
