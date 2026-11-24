// SPDX-License-Identifier: GPL-3.0-or-later
#include "kagami-ipc.hpp"

#include <cstdio>
#include <cstring>

namespace kagami {
namespace {

constexpr uint32_t kMagic = 0xD3D1110C;

enum class PktType : uint8_t {
	Unknown,
	Config,
	NeedData,
	HaveData,
	ReadDone,
	ReleaseData,
	Eos,
	Fin,
};

#pragma pack(push, 1)
struct PktHeader {
	PktType type;
	uint32_t payload_size;
	uint32_t magic;
};
#pragma pack(pop)

constexpr size_t kHeaderSize = sizeof(PktHeader); // 9 bytes

std::vector<uint8_t> BuildPacket(PktType type, const void *payload = nullptr, uint32_t size = 0)
{
	std::vector<uint8_t> buf(kHeaderSize + size);
	PktHeader header{type, size, kMagic};
	memcpy(buf.data(), &header, kHeaderSize);
	if (size)
		memcpy(buf.data() + kHeaderSize, payload, size);
	return buf;
}

bool ParseHeader(const std::vector<uint8_t> &buf, PktHeader &header)
{
	if (buf.size() < kHeaderSize)
		return false;
	memcpy(&header, buf.data(), kHeaderSize);
	return header.magic == kMagic && buf.size() == kHeaderSize + header.payload_size;
}

} // namespace

IpcClient::IpcClient()
{
	stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	io_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

IpcClient::~IpcClient()
{
	Close(false);
	if (io_event_)
		CloseHandle(io_event_);
	if (stop_event_)
		CloseHandle(stop_event_);
}

void IpcClient::Fail(const char *what, DWORD err)
{
	char buf[160];
	snprintf(buf, sizeof(buf), "%s (error %lu)", what, static_cast<unsigned long>(err));
	error_ = buf;
}

bool IpcClient::Connect(const std::wstring &pipe_name)
{
	Close(false);
	ResetEvent(stop_event_);

	HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				  FILE_FLAG_OVERLAPPED, nullptr);
	if (pipe == INVALID_HANDLE_VALUE) {
		Fail("pipe not available", GetLastError()); // normal while Kagami Cast is not sending
		return false;
	}
	DWORD mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) {
		Fail("SetNamedPipeHandleState failed", GetLastError());
		CloseHandle(pipe);
		return false;
	}
	pipe_ = pipe;
	return true;
}

void IpcClient::Close(bool send_fin)
{
	if (pipe_ != INVALID_HANDLE_VALUE) {
		if (send_fin)
			Send(BuildPacket(PktType::Fin));
		CancelIoEx(pipe_, nullptr);
		CloseHandle(pipe_);
		pipe_ = INVALID_HANDLE_VALUE;
	}
	if (server_process_) {
		CloseHandle(server_process_);
		server_process_ = nullptr;
	}
}

void IpcClient::Stop()
{
	SetEvent(stop_event_);
}

// One overlapped read or write that can be interrupted by Stop(). The byte count
// always comes from GetOverlappedResult (the ReadFile/WriteFile out-parameter is
// not reliable for overlapped handles). A message longer than the buffer yields
// more_data = true (ERROR_MORE_DATA); the rest is read by the next call.
IpcResult IpcClient::DoIo(bool write, void *buf, DWORD size, DWORD &transferred, bool &more_data)
{
	transferred = 0;
	more_data = false;
	OVERLAPPED ov{};
	ov.hEvent = io_event_;
	ResetEvent(io_event_);

	BOOL ok = write ? WriteFile(pipe_, buf, size, nullptr, &ov) : ReadFile(pipe_, buf, size, nullptr, &ov);
	DWORD err = ok ? ERROR_SUCCESS : GetLastError();
	if (!ok && err == ERROR_IO_PENDING) {
		HANDLE waits[] = {io_event_, stop_event_};
		if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
			CancelIoEx(pipe_, &ov);
			GetOverlappedResult(pipe_, &ov, &transferred, TRUE);
			return IpcResult::Stopped;
		}
	} else if (!ok && err != ERROR_MORE_DATA) {
		Fail(write ? "WriteFile failed" : "ReadFile failed", err); // ERROR_BROKEN_PIPE: sender went away
		return IpcResult::Error;
	}
	if (!GetOverlappedResult(pipe_, &ov, &transferred, FALSE)) {
		err = GetLastError();
		if (err != ERROR_MORE_DATA) {
			Fail("pipe I/O failed", err);
			return IpcResult::Error;
		}
		more_data = true;
	}
	return IpcResult::Ok;
}

bool IpcClient::Send(const std::vector<uint8_t> &msg)
{
	if (pipe_ == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	bool more = false;
	std::vector<uint8_t> copy(msg); // WriteFile wants a non-const buffer
	if (DoIo(true, copy.data(), static_cast<DWORD>(copy.size()), written, more) != IpcResult::Ok)
		return false;
	return written == msg.size();
}

IpcResult IpcClient::Receive(std::vector<uint8_t> &msg)
{
	if (pipe_ == INVALID_HANDLE_VALUE)
		return IpcResult::Error;
	msg.clear();
	std::vector<uint8_t> chunk(4096);
	for (;;) {
		DWORD got = 0;
		bool more = false;
		IpcResult r = DoIo(false, chunk.data(), static_cast<DWORD>(chunk.size()), got, more);
		if (r != IpcResult::Ok)
			return r;
		msg.insert(msg.end(), chunk.begin(), chunk.begin() + got);
		if (!more)
			return IpcResult::Ok;
	}
}

IpcResult IpcClient::ReadConfig(IpcConfig &config)
{
	std::vector<uint8_t> msg;
	IpcResult r = Receive(msg);
	if (r != IpcResult::Ok)
		return r;
	PktHeader header{};
	if (!ParseHeader(msg, header) || header.type != PktType::Config ||
	    header.payload_size <= sizeof(DWORD) + sizeof(int64_t)) {
		error_ = "expected CONFIG";
		return IpcResult::Error;
	}
	const uint8_t *p = msg.data() + kHeaderSize;
	memcpy(&config.server_pid, p, sizeof(DWORD));
	memcpy(&config.adapter_luid, p + sizeof(DWORD), sizeof(int64_t));
	const char *caps = reinterpret_cast<const char *>(p + sizeof(DWORD) + sizeof(int64_t));
	config.caps.assign(caps, strnlen(caps, header.payload_size - sizeof(DWORD) - sizeof(int64_t)));

	if (server_process_)
		CloseHandle(server_process_);
	server_process_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, config.server_pid);
	if (!server_process_) {
		Fail("OpenProcess(server) failed", GetLastError());
		return IpcResult::Error;
	}
	return IpcResult::Ok;
}

IpcResult IpcClient::NextFrame(IpcFrame &frame)
{
	if (!Send(BuildPacket(PktType::NeedData)))
		return WaitForSingleObject(stop_event_, 0) == WAIT_OBJECT_0 ? IpcResult::Stopped : IpcResult::Error;

	std::vector<uint8_t> msg;
	IpcResult r = Receive(msg);
	if (r != IpcResult::Ok)
		return r;
	PktHeader header{};
	if (!ParseHeader(msg, header)) {
		error_ = "broken packet";
		return IpcResult::Error;
	}
	if (header.type == PktType::Eos)
		return IpcResult::Eos;

	constexpr size_t fixed = sizeof(uint64_t) + sizeof(IpcMemLayout) + sizeof(HANDLE) + 1;
	if (header.type != PktType::HaveData || header.payload_size < fixed) {
		error_ = "expected HAVE-DATA";
		return IpcResult::Error;
	}
	const uint8_t *p = msg.data() + kHeaderSize;
	memcpy(&frame.pts, p, sizeof(uint64_t));
	p += sizeof(uint64_t);
	memcpy(&frame.layout, p, sizeof(IpcMemLayout));
	p += sizeof(IpcMemLayout);
	memcpy(&frame.server_handle, p, sizeof(HANDLE));
	p += sizeof(HANDLE);
	frame.caps.clear();
	if (*p++) {
		const char *caps = reinterpret_cast<const char *>(p);
		frame.caps.assign(caps, strnlen(caps, header.payload_size - fixed));
	}

	frame.handle = nullptr;
	if (!DuplicateHandle(server_process_, frame.server_handle, GetCurrentProcess(), &frame.handle, 0, FALSE,
			     DUPLICATE_SAME_ACCESS)) {
		Fail("DuplicateHandle failed", GetLastError());
		return IpcResult::Error;
	}
	if (!Send(BuildPacket(PktType::ReadDone))) {
		CloseHandle(frame.handle);
		frame.handle = nullptr;
		return IpcResult::Error;
	}
	return IpcResult::Ok;
}

bool IpcClient::Release(HANDLE server_handle)
{
	return Send(BuildPacket(PktType::ReleaseData, &server_handle, sizeof(HANDLE)));
}

bool CapsGetInt(const std::string &caps, const char *field, int &value)
{
	const std::string key = std::string(field) + "=(int)";
	size_t pos = caps.find(key);
	if (pos == std::string::npos)
		return false;
	value = atoi(caps.c_str() + pos + key.size());
	return true;
}

std::string CapsGetString(const std::string &caps, const char *field)
{
	const std::string key = std::string(field) + "=(string)";
	size_t pos = caps.find(key);
	if (pos == std::string::npos)
		return {};
	pos += key.size();
	size_t end = caps.find_first_of(",;", pos);
	return caps.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

} // namespace kagami
