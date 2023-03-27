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
#include <d3d11_4.h>
#include <string>

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
	HANDLE GetSharedTextureHandle();
private:
	bool SetupMediaFoundation();
	bool SetupCapture();

	bool SetupD3D11();
	bool SetupD3D11StagingTexture();
	bool SetupD3D11SharedTexture();

	ComPtr<IMFSourceReader> sourceReader;

	ComPtr<ID3D11Device5> device;
	ComPtr<ID3D11DeviceContext4> context;

	ComPtr<ID3D11Texture2D1> webcamStagingTexture;
	ComPtr<ID3D11Texture2D1> webcamSharedTexture;

	UINT width_{ 0 };
	UINT height_{ 0 };

};

bool WebcamApp::Initialize() {

	if (!SetupMediaFoundation()) {
		std::cerr << "Failed to set up Media Foundation." << std::endl;
		return false;
	}

	if (!SetupD3D11()) {
		std::cerr << "Failed to set up Direct3D 11." << std::endl;
		return false;
	}

	if (!SetupCapture()) {
		std::cerr << "Failed to set up webcam capture." << std::endl;
		return false;
	}

	if (!SetupD3D11StagingTexture()) {
		std::cerr << "Failed to set up D3D11 staging texture." << std::endl;
		return false;
	}

	if (!SetupD3D11SharedTexture()) {
		std::cerr << "Failed to set up D3D11 shared texture." << std::endl;
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

bool WebcamApp::SetupD3D11() {
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1 };
	UINT creationFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#if defined(_DEBUG)
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevel;
	ComPtr<ID3D11Device> _device;
	ComPtr<ID3D11DeviceContext> _context;
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels, 1, D3D11_SDK_VERSION, &_device, &featureLevel, &_context);
	if (FAILED(hr)) {
		std::cerr << "Failed to crate D3D11 device and context." << std::endl;
	}

	hr = _device->QueryInterface(__uuidof(ID3D11Device5), (void**)&device);
	if (FAILED(hr)) {
		std::cerr << "Failed to upgrade D3D11 device to latest." << std::endl;
		return false;
	}

	hr = _context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&context);
	if (FAILED(hr)) {
		std::cerr << "Failed to upgrade D3D11 context to latest." << std::endl;
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

bool WebcamApp::SetupD3D11StagingTexture() {

	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_YUY2;
	D3D11_TEXTURE2D_DESC1 textureDesc = {};
	textureDesc.Width = width_;
	textureDesc.Height = height_;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = dxgiFormat;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_STAGING;
	textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	textureDesc.BindFlags = 0;
	textureDesc.MiscFlags = 0;

	HRESULT hr = device->CreateTexture2D1(&textureDesc, nullptr, webcamStagingTexture.GetAddressOf());
	if (FAILED(hr)) {
		std::cerr << "Failed to create webcam D3D11 staging texture #1." << std::endl;
		return false;
	}

	return true;
}

bool WebcamApp::SetupD3D11SharedTexture() {
	DXGI_FORMAT dxgiFormat = DXGI_FORMAT_YUY2;
	D3D11_TEXTURE2D_DESC1 textureDesc = {};
	textureDesc.Width = width_;
	textureDesc.Height = height_;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = dxgiFormat;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.BindFlags = 0;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	HRESULT hr = device->CreateTexture2D1(&textureDesc, nullptr, webcamSharedTexture.GetAddressOf());
	if (FAILED(hr)) {
		std::cerr << "Failed to create D3D11 (shared) rendering texture." << std::endl;
		return false;
	}

	return true;
}

HANDLE WebcamApp::GetSharedTextureHandle() {
	if (!webcamSharedTexture)
		return nullptr;

	ComPtr<IDXGIResource1> dxgiResource;
	HRESULT hr = webcamSharedTexture.As(&dxgiResource);
	if (FAILED(hr)) {
		std::cerr << "Failed to fetch IDXGIResource1 interface from the rendering texture." << std::endl;
		return nullptr;
	}
	hr = webcamSharedTexture->QueryInterface(__uuidof(IDXGIResource1), (void**)&dxgiResource);
	if (FAILED(hr)) {
		std::cerr << "Failed to query DXGIResource1 from the rendering texture." << std::endl;
		return nullptr;
	}

	HANDLE sharedHandle;
	hr = dxgiResource->GetSharedHandle(&sharedHandle);
	if (FAILED(hr)) {
		std::cerr << "Failed to create shared handle for the rendering texture." << std::endl;
		return nullptr;
	}

	std::string handleStr = std::to_string((long long)sharedHandle);

	std::cout << "Shared texture handle is " << sharedHandle << " | " << handleStr << std::endl;;
	std::cout << "Texture width/height is " << width_ << "/" << height_ << std::endl;

	return sharedHandle;

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

	D3D11_MAPPED_SUBRESOURCE mappedResource;

	BYTE* pScanline0 = nullptr;
	LONG pitch;

	GetSharedTextureHandle();

	while (true) {

		streamIndex = 0;
		flags = 0;
		timestamp = 0;

		if (GetAsyncKeyState(VK_ESCAPE)) {
			break;
		}

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

				hr = context->Map(webcamStagingTexture.Get(), NULL, D3D11_MAP_WRITE, 0, &mappedResource);
				if (FAILED(hr)) {
					std::cerr << "Failed to map the webcam staging texture." << std::endl;
					pBuffer2D2.Reset();
					break;
				}

				size_t bufferSize = static_cast<size_t>(height_) * pitch;

				memcpy(mappedResource.pData, pScanline0, bufferSize);

				context->Unmap(webcamStagingTexture.Get(), 0);

				pBuffer2D2->Unlock2D();

				context->CopyResource(webcamSharedTexture.Get(), webcamStagingTexture.Get());

				srcData = nullptr;

				pBuffer2D2.Reset();
			}

			useBuffer0 = !useBuffer0;

			buffer.Reset();
			sample.Reset();
		}
	}

	pBuffer2D2.Reset();
	buffer.Reset();
	sample.Reset();
	srcData = nullptr;
}

void WebcamApp::Cleanup() {
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
