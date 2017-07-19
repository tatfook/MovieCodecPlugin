#include "stdafx.h"
#include <vector>
#include "AudioCapture.h"

using namespace ParaEngine;

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096


ParaEngine::CAudioCapture::CAudioCapture()
:m_hThread(NULL), m_bIsCapturing(false), m_hStartedEvent(NULL), m_hStopEvent(NULL), m_pAudioClient(NULL), m_pAudioCaptureClient(NULL),
m_hTask(NULL), m_bFirstPacket(true), m_nBlockAlign(0), m_nPasses(0), m_nTimerInterval(10)
{
}

ParaEngine::CAudioCapture::~CAudioCapture()
{
}

int ParaEngine::CAudioCapture::BeginCaptureInThread()
{
	if (m_bIsCapturing)
		EndCaptureInThread();
	
	IMMDevice *pMMDevice = prefs.m_pMMDevice;
	bool bInt16 = prefs.m_bInt16;

	//PUINT32 pnFrames
	HRESULT hr = E_UNEXPECTED;

	// activate an IAudioClient
	hr = pMMDevice->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL, NULL,
		(void**)&m_pAudioClient
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
		return hr;
	}

	// get the default device periodicity
	REFERENCE_TIME hnsDefaultDevicePeriod;
	hr = m_pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioClient::GetDevicePeriod failed: hr = 0x%08x\n", hr);
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	// get the default device format
	WAVEFORMATEX *pwfx;
	hr = m_pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioClient::GetMixFormat failed: hr = 0x%08x\n", hr);
		CoTaskMemFree(pwfx);
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	if (bInt16) {
		// coerce int-16 wave format
		// can do this in-place since we're not changing the size of the format
		// also, the engine will auto-convert from float to int for us
		switch (pwfx->wFormatTag) {
		case WAVE_FORMAT_IEEE_FLOAT:
			pwfx->wFormatTag = WAVE_FORMAT_PCM;
			pwfx->wBitsPerSample = 16;
			pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
			break;

		case WAVE_FORMAT_EXTENSIBLE:
			{
				// naked scope for case-local variable
				PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
				if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
					pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
					pEx->Samples.wValidBitsPerSample = 16;
					pwfx->wBitsPerSample = 16;
					pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
					pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
				}
				else {
					OUTPUT_LOG("Don't know how to coerce mix format to int-16\n");
					CoTaskMemFree(pwfx);
					m_pAudioClient->Release();
					m_pAudioClient = NULL;
					return E_UNEXPECTED;
				}
			}
			break;
		default:
			OUTPUT_LOG("Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16\n", pwfx->wFormatTag);
			CoTaskMemFree(pwfx);
			m_pAudioClient->Release();
			m_pAudioClient = NULL;
			return E_UNEXPECTED;
		}
	}

	m_nBlockAlign = pwfx->nBlockAlign;
	m_nBitsPerSample = pwfx->wBitsPerSample;
	m_nChannels = pwfx->nChannels;
	m_nSamplesPerSec = pwfx->nSamplesPerSec;
	if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
		OUTPUT_LOG("valid bit per sample %d\n", pEx->Samples.wValidBitsPerSample);
	}

	// Xizhi Fix: 100 ms buffer: allocate a bigger buffer to avoid glitches 
	REFERENCE_TIME audio_buffer_duration = max(hnsDefaultDevicePeriod, (10*1000)*100);

	// call IAudioClient::Initialize
	// note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK
	// do not work together...
	// the "data ready" event never gets set
	// so we're going to do a timer-driven loop
	hr = m_pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK,
		audio_buffer_duration, 0, pwfx, 0
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioClient::Initialize failed: hr = 0x%08x\n", hr);
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}
	CoTaskMemFree(pwfx);

	// activate an IAudioCaptureClient
	hr = m_pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&m_pAudioCaptureClient
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioClient::GetService(IAudioCaptureClient) failed: hr 0x%08x\n", hr);
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	// register with MMCSS
	DWORD nTaskIndex = 0;
	HANDLE m_hTask = AvSetMmThreadCharacteristics("Capture", &nTaskIndex);
	if (NULL == m_hTask) {
		DWORD dwErr = GetLastError();
		OUTPUT_LOG("AvSetMmThreadCharacteristics failed: last error = %u\n", dwErr);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return HRESULT_FROM_WIN32(dwErr);
	}

	// set the waitable timer
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
	m_nTimerInterval = (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds
	
	// call IAudioClient::Start
	hr = m_pAudioClient->Start();
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioClient::Start failed: hr = 0x%08x\n", hr);
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	m_bIsCapturing = true;
	m_bFirstPacket = true;
	m_nPasses = 0;
	return 0;
}

int ParaEngine::CAudioCapture::EndCaptureInThread()
{
	if (!m_bIsCapturing)
		return 0;
	m_bIsCapturing = false;

	if (m_pAudioClient)
	{
		m_pAudioClient->Stop();
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		m_pAudioCaptureClient = NULL;
	}
	return 0;
}

int ParaEngine::CAudioCapture::FrameCaptureInThread(const BYTE ** ppData, int * pByteCount)
{
	if (!m_pAudioClient)
		return 0;
	m_nPasses++;

	// got a "wake up" event - see if there's data
	UINT32 nNextPacketSize;
	HRESULT hr = m_pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioCaptureClient::GetNextPacketSize failed on pass %u frames: hr = 0x%08x\n", m_nPasses, hr);
		m_pAudioClient->Stop();
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioCaptureClient = NULL;
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	if (0 == nNextPacketSize) {
		// no data yet
		return 0;
	}

	// get the captured data
	BYTE *pData;
	UINT32 nNumFramesToRead = 0;
	DWORD dwFlags;

	hr = m_pAudioCaptureClient->GetBuffer(
		&pData,
		&nNumFramesToRead,
		&dwFlags,
		NULL,
		NULL
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioCaptureClient::GetBuffer failed on pass %u hr = 0x%08x\n", m_nPasses, hr);
		m_pAudioClient->Stop();
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	if (m_bFirstPacket && AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY == dwFlags) {
		OUTPUT_LOG("Warning: Probably spurious glitch reported on first packet\n");
	}
	else if (0 != dwFlags) {
		if (dwFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
		{
			OUTPUT_LOG("Warning in AudioCapture: Probably spurious glitch reported on pass %u\n", m_nPasses);
		}
		if (dwFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
		{
			OUTPUT_LOG("Warning in AudioCapture: timestamp error reported on pass %u\n", m_nPasses);
		}
		{
			/*OUTPUT_LOG("Error in AudioCapture: IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u frames\n", dwFlags, m_nPasses);
			m_pAudioClient->Stop();
			AvRevertMmThreadCharacteristics(m_hTask);
			m_pAudioCaptureClient->Release();
			m_pAudioClient->Release();
			m_pAudioClient = NULL;
			return E_UNEXPECTED;*/
		}
	}

	if (0 == nNumFramesToRead) {
		OUTPUT_LOG("IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u frames\n", m_nPasses);
		m_pAudioClient->Stop();
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return E_UNEXPECTED;
	}

	LONG lBytesToWrite = nNumFramesToRead * m_nBlockAlign;
#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")

	// copy to buffer
	static std::vector<BYTE> g_buffer;
	if ( (int) g_buffer.size() < lBytesToWrite)
	{
		g_buffer.resize(lBytesToWrite);
		memset(&(g_buffer[0]), 0, lBytesToWrite);
	}
	if (ppData || pByteCount)
	{
		memcpy(&(g_buffer[0]), pData, lBytesToWrite);
		*ppData = &(g_buffer[0]);
		*pByteCount = lBytesToWrite;
	}

	hr = m_pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
	if (FAILED(hr)) {
		OUTPUT_LOG("IAudioCaptureClient::ReleaseBuffer failed on pass %u after hr = 0x%08x\n", m_nPasses, hr);
		m_pAudioClient->Stop();
		AvRevertMmThreadCharacteristics(m_hTask);
		m_pAudioCaptureClient->Release();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
		return hr;
	}

	m_bFirstPacket = false;
	return 0;
}

int ParaEngine::CAudioCapture::GetFrameByteCount()
{
	return m_nBlockAlign;
}

////////////////////////////////////////////
#pragma region prefs

#include <functiondiscoverykeys_devpkey.h>
#define DEFAULT_FILE "loopback-capture.wav"

HRESULT get_default_device(IMMDevice **ppMMDevice);
HRESULT list_devices();
HRESULT get_specific_device(LPCWSTR szLongName, IMMDevice **ppMMDevice);

CPrefs::CPrefs()
: m_pMMDevice(NULL)
, m_hFile(NULL)
, m_bInt16(true)
, m_pwfx(NULL)
, m_szFilename(NULL)
{
	HRESULT hr;
	// open default device if not specified
	if (NULL == m_pMMDevice) {
		hr = get_default_device(&m_pMMDevice);
		if (FAILED(hr)) {
			return;
		}
	}
}

CPrefs::~CPrefs() {
	if (NULL != m_pMMDevice) {
		m_pMMDevice->Release();
	}

	if (NULL != m_hFile) {
		mmioClose(m_hFile, 0);
	}

	if (NULL != m_pwfx) {
		CoTaskMemFree(m_pwfx);
	}
}

HRESULT get_default_device(IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x\n", hr);
		return hr;
	}

	// get the default render endpoint
	hr = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, ppMMDevice);
	pMMDeviceEnumerator->Release();
	if (FAILED(hr)) {
		OUTPUT_LOG("IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: hr = 0x%08x\n", hr);
		return hr;
	}

	return S_OK;
}

HRESULT list_devices() {
	HRESULT hr = S_OK;

	// get an enumerator
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x\n", hr);
		return hr;
	}

	IMMDeviceCollection *pMMDeviceCollection;

	// get all the active render endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
		);
	pMMDeviceEnumerator->Release();
	if (FAILED(hr)) {
		OUTPUT_LOG("IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x\n", hr);
		return hr;
	}

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		pMMDeviceCollection->Release();
		OUTPUT_LOG("IMMDeviceCollection::GetCount failed: hr = 0x%08x\n", hr);
		return hr;
	}
	OUTPUT_LOG("Active render endpoints found: %u\n", count);

	for (UINT i = 0; i < count; i++) {
		IMMDevice *pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IMMDeviceCollection::Item failed: hr = 0x%08x\n", hr);
			return hr;
		}

		// open the property store on that device
		IPropertyStore *pPropertyStore;
		hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
		pMMDevice->Release();
		if (FAILED(hr)) {
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IMMDevice::OpenPropertyStore failed: hr = 0x%08x\n", hr);
			return hr;
		}

		// get the long name property
		PROPVARIANT pv; PropVariantInit(&pv);
		hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
		pPropertyStore->Release();
		if (FAILED(hr)) {
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IPropertyStore::GetValue failed: hr = 0x%08x\n", hr);
			return hr;
		}

		if (VT_LPWSTR != pv.vt) {
			OUTPUT_LOG("PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);

			PropVariantClear(&pv);
			pMMDeviceCollection->Release();
			return E_UNEXPECTED;
		}

		OUTPUT_LOG("    %ls\n", pv.pwszVal);

		PropVariantClear(&pv);
	}
	pMMDeviceCollection->Release();

	return S_OK;
}

HRESULT get_specific_device(LPCWSTR szLongName, IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;

	*ppMMDevice = NULL;

	// get an enumerator
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
		);
	if (FAILED(hr)) {
		OUTPUT_LOG("CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x\n", hr);
		return hr;
	}

	IMMDeviceCollection *pMMDeviceCollection;

	// get all the active render endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
		);
	pMMDeviceEnumerator->Release();
	if (FAILED(hr)) {
		OUTPUT_LOG("IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x\n", hr);
		return hr;
	}

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		pMMDeviceCollection->Release();
		OUTPUT_LOG("IMMDeviceCollection::GetCount failed: hr = 0x%08x\n", hr);
		return hr;
	}

	for (UINT i = 0; i < count; i++) {
		IMMDevice *pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IMMDeviceCollection::Item failed: hr = 0x%08x\n", hr);
			return hr;
		}

		// open the property store on that device
		IPropertyStore *pPropertyStore;
		hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
		if (FAILED(hr)) {
			pMMDevice->Release();
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IMMDevice::OpenPropertyStore failed: hr = 0x%08x\n", hr);
			return hr;
		}

		// get the long name property
		PROPVARIANT pv; PropVariantInit(&pv);
		hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
		pPropertyStore->Release();
		if (FAILED(hr)) {
			pMMDevice->Release();
			pMMDeviceCollection->Release();
			OUTPUT_LOG("IPropertyStore::GetValue failed: hr = 0x%08x\n", hr);
			return hr;
		}

		if (VT_LPWSTR != pv.vt) {
			OUTPUT_LOG("PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);

			PropVariantClear(&pv);
			pMMDevice->Release();
			pMMDeviceCollection->Release();
			return E_UNEXPECTED;
		}

		// is it a match?
		if (0 == _wcsicmp(pv.pwszVal, szLongName)) {
			// did we already find it?
			if (NULL == *ppMMDevice) {
				*ppMMDevice = pMMDevice;
				pMMDevice->AddRef();
			}
			else {
				OUTPUT_LOG("Found (at least) two devices named %ls\n", szLongName);
				PropVariantClear(&pv);
				pMMDevice->Release();
				pMMDeviceCollection->Release();
				return E_UNEXPECTED;
			}
		}

		pMMDevice->Release();
		PropVariantClear(&pv);
	}
	pMMDeviceCollection->Release();

	if (NULL == *ppMMDevice) {
		OUTPUT_LOG("Could not find a device named %ls\n", szLongName);
		return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	return S_OK;
}
#pragma endregion