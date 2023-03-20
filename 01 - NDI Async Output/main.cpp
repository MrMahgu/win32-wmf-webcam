#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>
#include <wrl/client.h>
#include <iostream>

#include "inc/Processing.NDI.Lib.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

class WebcamApp {
public:
	bool Initialize();
	void Run();
	void Cleanup();
private:
	bool SetupMediaFoundation();
	bool SetupCapture();
	bool SetupNDI();

	bool LoadNDIRuntime();
	bool CreateNDISender();
	void InitializeNDIFrame();
	void CleanupNDI();

	bool CreateBuffers();
	void DestroyBuffers();

	ComPtr<IMFSourceReader> sourceReader;

	UINT width_{ 0 };
	UINT height_{ 0 };

	const NDIlib_v5* ndiLib_v5_{ nullptr };

	NDIlib_send_instance_t ndi_sender_{ nullptr };
	NDIlib_video_frame_v2_t ndi_video_frame_{ NULL };

	uint8_t* buffer1_;
	uint8_t* buffer2_;
};

bool WebcamApp::Initialize() {

	if (!SetupMediaFoundation()) {
		std::cerr << "Failed to set up Media Foundation." << std::endl;
		return false;
	}

	if (!SetupCapture()) {
		std::cerr << "Failed to set up webcam capture." << std::endl;
		return false;
	}

	if (!SetupNDI()) {
		std::cerr << "Failed to set up NDI." << std::endl;
		return false;
	}

	if (!CreateBuffers()) {
		std::cerr << "Failed to create buffers required for NDI." << std::endl;
		return false;
	}

	return true;
}

bool WebcamApp::SetupMediaFoundation() {
	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		std::cerr << "Failed to initialize Media Foundation." << std::endl;
		return false;
	}
	return true;
}

void PrintError(HRESULT hr) {
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		hr,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0,
		nullptr);

	std::wcerr << L"Error: " << (LPCTSTR)lpMsgBuf << std::endl;

	LocalFree(lpMsgBuf);
}

void CleanupActivateArray(IMFActivate** activateArray, UINT32 count)
{
	if (activateArray != nullptr) {
		for (UINT32 i = 0; i < count; i++) {
			if (activateArray[i] != nullptr) {
				activateArray[i]->Release();
			}
		}
		CoTaskMemFree(activateArray);
	}
}

bool WebcamApp::SetupCapture() {
	ComPtr<IMFAttributes> attributes;
	HRESULT hr = MFCreateAttributes(&attributes, 1);
	if (FAILED(hr)) {
		std::cerr << "Failed to create IMFAttributes." << std::endl;
		return false;
	}

	hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if (FAILED(hr)) {
		std::cerr << "Failed to set device source attribute." << std::endl;
		return false;
	}

	IMFActivate** activateArray = nullptr;
	UINT32 count;
	hr = MFEnumDeviceSources(attributes.Get(), &activateArray, &count);
	if (FAILED(hr) || count == 0) {
		std::cerr << "Failed to enumerate video capture devices." << std::endl;
		CoTaskMemFree(activateArray);
		return false;
	}

	for (UINT32 i = 0; i < count; i++) {
		WCHAR* friendlyName = nullptr;
		UINT32 friendlyNameLength = 0;

		hr = activateArray[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &friendlyNameLength);
		if (SUCCEEDED(hr)) {
			std::wcout << L"Device " << i << L": " << friendlyName << std::endl;
		}
		else
		{
			std::cerr << "Failed to get friendly name for device " << i << std::endl;
		}
	}

	ComPtr<IMFMediaSource> mediaSource;
	hr = activateArray[0]->ActivateObject(IID_PPV_ARGS(&mediaSource));
	if (FAILED(hr) || count == 0) {
		std::cerr << "Failed to activate IMFMediaSource." << std::endl;
		CleanupActivateArray(activateArray, count);
		return false;
	}

	hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &sourceReader);
	if (FAILED(hr)) {
		std::cerr << "Failed to create IMFSourceReader from IMFMediaSource." << std::endl;
		CleanupActivateArray(activateArray, count);
		return false;
	}

	CleanupActivateArray(activateArray, count);

	ComPtr<IMFMediaType> mediaType;
	hr = sourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mediaType);
	if (FAILED(hr)) {
		std::cerr << "Failed to get current media type." << std::endl;
		return false;
	}

	UINT32 width, height;
	hr = MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &width, &height);
	if (FAILED(hr)) {
		std::cerr << "Failed to get video frame size." << std::endl;
		return false;
	}

	width_ = width;
	height_ = height;

	GUID format;
	hr = mediaType->GetGUID(MF_MT_SUBTYPE, &format);
	if (FAILED(hr)) {
		std::cerr << "Failed to get format." << std::endl;
		return false;
	}

	hr = sourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mediaType.Get());
	if (FAILED(hr)) {
		std::cerr << "Failed to set video output format" << std::endl;
		return false;
	}

	hr = sourceReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
	if (FAILED(hr)) {
		std::cerr << "Failed to enable video stream" << std::endl;
		return false;
	}

	return true;
}

bool WebcamApp::SetupNDI() {

	if (!LoadNDIRuntime()) {
		return false;
	}

	if (!ndiLib_v5_->initialize()) {
		std::cerr << "NDI reported your CPU as invalid and will not run." << std::endl;
		return false;
	}

	if (!CreateNDISender()) {
		return false;
	}

	InitializeNDIFrame();

	return true;
}

bool WebcamApp::LoadNDIRuntime() {

	// DynamicLoad NDI

	char* p_ndi_runtime_v5;
	size_t len;
	errno_t err = _dupenv_s(&p_ndi_runtime_v5, &len, NDILIB_REDIST_FOLDER);

	if (err != 0 || p_ndi_runtime_v5 == NULL) {
		std::cerr << "Failed to detect a valida NDI5 installation folder." << std::endl;
		std::cerr << "Error: " << err << std::endl;
		return false;
	}

	std::string ndi_path = p_ndi_runtime_v5;
	ndi_path += "\\" NDILIB_LIBRARY_NAME;

	HMODULE hNDILib = LoadLibraryA(ndi_path.c_str());

	using NDIlib_v5_load_func = const NDIlib_v5* (*)(void);
	NDIlib_v5_load_func NDIlib_v5_load = nullptr;

	if (hNDILib) {
		NDIlib_v5_load = reinterpret_cast<NDIlib_v5_load_func>(GetProcAddress(hNDILib, "NDIlib_v5_load"));
	}

	if (!NDIlib_v5_load) {
		if (hNDILib)
			FreeLibrary(hNDILib);

		std::cerr << "NDI5 detected however a valid library could not be loaded." << std::endl;
		return false;
	}

	ndiLib_v5_ = NDIlib_v5_load();

	return true;
}

bool WebcamApp::CreateNDISender() {
	NDIlib_send_create_t ndi_sender_desc;
	ndi_sender_desc.p_ndi_name = "webcam_to_ndi";

	ndi_sender_ = ndiLib_v5_->send_create(&ndi_sender_desc);
	if (!ndi_sender_) {
		std::cerr << "Could not created NDI sender '" << ndi_sender_desc.p_ndi_name << "'" << std::endl;
		return false;
	}

	return true;
}

void WebcamApp::InitializeNDIFrame() {
	ndi_video_frame_.FourCC = NDIlib_FourCC_type_UYVY;
	ndi_video_frame_.xres = width_;
	ndi_video_frame_.yres = height_;
	ndi_video_frame_.line_stride_in_bytes = width_ * 2;
	ndi_video_frame_.frame_rate_D = 1000;
	ndi_video_frame_.frame_rate_N = 60000;
}

void WebcamApp::CleanupNDI() {
	ndiLib_v5_->send_send_video_async_v2(ndi_sender_, NULL);
	ndiLib_v5_->send_destroy(ndi_sender_);
	ndiLib_v5_->destroy();
}

void YUY2ToUYVYWithPitch(const BYTE* srcData, BYTE* destData, UINT width, UINT height, LONG pitch) {
	for (UINT y = 0; y < height; ++y) {
		const BYTE* srcRow = srcData + y * pitch;
		BYTE* destRow = destData + y * width * 2;

		for (UINT x = 0; x < width; x += 2) {
			UINT idx = x * 2;
			destRow[idx] = srcRow[idx + 1];     // U
			destRow[idx + 1] = srcRow[idx];     // Y0
			destRow[idx + 2] = srcRow[idx + 3]; // V
			destRow[idx + 3] = srcRow[idx + 2]; // Y1
		}
	}
}

void WebcamApp::Run() {
	bool useBuffer0 = true;

	DWORD streamIndex, flags;
	LONGLONG timestamp;
	BYTE* srcData = nullptr;
	DWORD currentLength;
	UINT rowBytesYUY2 = width_ * 2;
	UINT totalBytesYUY2 = height_ * rowBytesYUY2;

	ComPtr<IMF2DBuffer2> pBuffer2D2;
	ComPtr<IMFMediaBuffer> buffer;

	ComPtr<IMFSample> sample;

	BYTE* pScanline0 = nullptr;
	LONG pitch;

	//int index = 0;

	while (true) {

		streamIndex = 0;
		flags = 0;
		timestamp = 0;

		// Control-C has no cleanup code and I need to exit, lol
		//index++;
		//if (index >= 10000) {
		//	break;
		//}

		HRESULT hr = sourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &timestamp, sample.GetAddressOf());
		if (FAILED(hr)) {
			std::cerr << "Failed to read video sample." << std::endl;
			break;
		}

		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			std::cout << "End of stream." << std::endl;
			break;
		}

		if (sample) {

			hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
			if (FAILED(hr)) {
				std::cerr << "Failed to convert sample to contiguous buffer." << std::endl;
				break;
			}

			hr = buffer.As(&pBuffer2D2);
			if (SUCCEEDED(hr)) {

				hr = pBuffer2D2->Lock2DSize(MF2DBuffer_LockFlags_Read, &srcData, &pitch, &pScanline0, &currentLength);

				if (FAILED(hr)) {
					std::cerr << "Failed to convert sample to contiguous buffer (2d2)." << std::endl;
					pBuffer2D2.Reset();
					break;
				}

				if (useBuffer0) {
					YUY2ToUYVYWithPitch(srcData, buffer1_, width_, height_, pitch);
					ndi_video_frame_.p_data = buffer2_;
				}
				else {
					YUY2ToUYVYWithPitch(srcData, buffer2_, width_, height_, pitch);
					ndi_video_frame_.p_data = buffer1_;
				}

				ndiLib_v5_->send_send_video_async_v2(ndi_sender_, &ndi_video_frame_);

				pBuffer2D2->Unlock2D();

				srcData = nullptr;

				pBuffer2D2.Reset();
			}

			useBuffer0 = !useBuffer0;

			buffer.Reset();
			sample.Reset();
		}
	}
}

bool WebcamApp::CreateBuffers() {
	buffer1_ = static_cast<uint8_t*>(_aligned_malloc(width_ * height_ * 2, 64));
	buffer2_ = static_cast<uint8_t*>(_aligned_malloc(width_ * height_ * 2, 64));
	return true;
}

void WebcamApp::DestroyBuffers() {
	if (buffer1_) {
		std::cout << "Freed buffer 1" << std::endl;
		_aligned_free(buffer1_);
	}
	if (buffer2_) {
		std::cout << "Freed buffer 2" << std::endl;
		_aligned_free(buffer2_);
	}
}

void WebcamApp::Cleanup() {
	CleanupNDI();
	DestroyBuffers();
	MFShutdown();
}

int main() {
	WebcamApp app;

	if (!app.Initialize()) {
		std::cerr << "Failed to initialize webcam application." << std::endl;
		return 1;
	}

	app.Run();
	app.Cleanup();

	return 0;
}
