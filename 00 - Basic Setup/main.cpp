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
private:
	bool SetupMediaFoundation();
	bool SetupCapture();

	ComPtr<IMFSourceReader> sourceReader;
	UINT width_{ 0 };
	UINT height_{ 0 };
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
		for (UINT32 i = 0; i < count; i++) {
			activateArray[i]->Release();
		}
		CoTaskMemFree(activateArray);
		return false;
	}

	hr = MFCreateSourceReaderFromMediaSource(mediaSource.Get(), nullptr, &sourceReader);
	if (FAILED(hr)) {
		std::cerr << "Failed to create IMFSourceReader from IMFMediaSource." << std::endl;
		for (UINT32 i = 0; i < count; i++) {
			activateArray[i]->Release();
		}
		CoTaskMemFree(activateArray);
		return false;
	}

	for (UINT32 i = 0; i < count; i++) {
		activateArray[i]->Release();
	}

	CoTaskMemFree(activateArray);

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

	width_ = width;
	height_ = height;

	return true;
}

void WebcamApp::Run() {
	bool useBuffer0 = true;

	DWORD streamIndex, flags;
	LONGLONG timestamp;
	BYTE* srcData = nullptr;
	DWORD currentLength;

	ComPtr<IMF2DBuffer2> pBuffer2D2;
	ComPtr<IMFMediaBuffer> buffer;

	ComPtr<IMFSample> sample;

	BYTE* pScanline0 = nullptr;
	LONG pitch;

	while (true) {

		streamIndex = 0;
		flags = 0;
		timestamp = 0;

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

				// can access srcData here

				pBuffer2D2->Unlock2D();

				srcData = nullptr;

				pBuffer2D2.Reset();
			}

			buffer.Reset();
			sample.Reset();
		}
	}
}

int main() {
	WebcamApp app;

	if (!app.Initialize()) {
		std::cerr << "Failed to initialize webcam application." << std::endl;
		return 1;
	}

	app.Run();

	return 0;
}
