#pragma  once

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <avrt.h>

namespace ParaEngine
{
	class CPrefs {
	public:
		IMMDevice *m_pMMDevice;
		HMMIO m_hFile;
		bool m_bInt16;
		PWAVEFORMATEX m_pwfx;
		LPCSTR m_szFilename;

		// set hr to S_FALSE to abort but return success
		CPrefs();
		~CPrefs();
	};

	/** audio capture */
	class CAudioCapture
	{
	public:
		CAudioCapture();
		~CAudioCapture();
	public:

		int BeginCaptureInThread();

		int FrameCaptureInThread(const BYTE ** ppData, int * pByteCount);

		void Pause();

		void Resume();

		int EndCaptureInThread();

		/*get the data byte count for a single frame.  */
		int GetFrameByteCount();

		int GetChannels(){ return m_nChannels; };

		int GetSampleRate(){ return m_nSamplesPerSec; };
		
		int GetTimerInterval(){ return m_nTimerInterval; }

		int GetBitsPerSample(){ return m_nBitsPerSample; }
	private:
		CPrefs prefs;
		
		bool m_bIsCapturing;

		HANDLE m_hThread;
		HANDLE m_hStartedEvent;
		HANDLE m_hStopEvent;
		
		IAudioClient* m_pAudioClient;
		IAudioCaptureClient *m_pAudioCaptureClient;

		HANDLE m_hTask;
		bool m_bFirstPacket;
		UINT32 m_nBlockAlign;
		int m_nBitsPerSample;
		int m_nChannels;
		int m_nSamplesPerSec;
		int m_nPasses;
		LONG m_nTimerInterval;
	};
}