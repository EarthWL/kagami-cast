// SPDX-License-Identifier: GPL-3.0-or-later
//
// Client side of GStreamer's d3d11ipcsink protocol (gst-plugins-bad 1.28,
// sys/d3d11/gstd3d11ipc*.cpp), re-implemented without GStreamer so it can run
// inside an OBS plugin. Kagami Cast starts uxplay with
//   -vs "... ! d3d11ipcsink pipe-name=<pipe> adapter=<n>"
// and this client receives the decoded frames as shared D3D11 textures.
//
// Message flow (all messages are one named-pipe message each):
//   server -> CONFIG {server pid, adapter LUID, caps}
//   loop:  client -> NEED_DATA ; server -> HAVE_DATA {pts, layout, handle[, caps]}
//          client duplicates the NT handle from the server process, opens it with
//          ID3D11Device1::OpenSharedResource1 (keyed mutex, key 0) and answers
//          READ_DONE; once it no longer uses the texture: RELEASE_DATA {handle}
//   end:   server -> EOS ; client -> FIN
// The texture can only be opened on the adapter given in CONFIG.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace kagami {

#pragma pack(push, 1)
struct IpcMemLayout {
	uint32_t size;
	uint32_t pitch;
	uint32_t offset[4];
};
#pragma pack(pop)

struct IpcConfig {
	DWORD server_pid = 0;
	int64_t adapter_luid = 0; // (HighPart << 32) | LowPart, as GStreamer sends it
	std::string caps;         // serialized GstCaps, e.g. "video/x-raw(memory:D3D11Memory), format=(string)BGRA, ..."
};

struct IpcFrame {
	uint64_t pts = 0;              // GstClockTime of the server, nanoseconds
	IpcMemLayout layout{};
	HANDLE server_handle = nullptr; // identifies the frame for Release()
	HANDLE handle = nullptr;        // duplicated into this process; caller must CloseHandle()
	std::string caps;               // non-empty only when the format changed
};

enum class IpcResult { Ok, Eos, Stopped, Error };

class IpcClient {
public:
	IpcClient();
	~IpcClient();
	IpcClient(const IpcClient &) = delete;
	IpcClient &operator=(const IpcClient &) = delete;

	// One connection attempt; false while the server pipe does not exist yet.
	bool Connect(const std::wstring &pipe_name);
	bool Connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

	IpcResult ReadConfig(IpcConfig &config);
	// NEED_DATA -> HAVE_DATA -> READ_DONE. On Ok the caller owns frame.handle.
	IpcResult NextFrame(IpcFrame &frame);
	// The texture behind server_handle is no longer used.
	bool Release(HANDLE server_handle);
	// Sends FIN when asked and still connected, then closes everything.
	void Close(bool send_fin);
	// Thread-safe: makes a blocked ReadConfig/NextFrame return Stopped.
	void Stop();

	const std::string &LastError() const { return error_; }

private:
	bool Send(const std::vector<uint8_t> &msg);
	IpcResult Receive(std::vector<uint8_t> &msg);
	IpcResult DoIo(bool write, void *buf, DWORD size, DWORD &transferred, bool &more_data);
	void Fail(const char *what, DWORD err);

	HANDLE pipe_ = INVALID_HANDLE_VALUE;
	HANDLE server_process_ = nullptr;
	HANDLE stop_event_ = nullptr;
	HANDLE io_event_ = nullptr;
	std::string error_;
};

// Minimal readers for fields of a serialized GstCaps string.
bool CapsGetInt(const std::string &caps, const char *field, int &value);
std::string CapsGetString(const std::string &caps, const char *field);

} // namespace kagami
