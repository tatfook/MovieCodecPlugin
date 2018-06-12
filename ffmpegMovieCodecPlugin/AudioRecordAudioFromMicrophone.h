#pragma once
#include <string>

class AudioRecordAudioFromMicrophone
{
public:
	AudioRecordAudioFromMicrophone();

	~AudioRecordAudioFromMicrophone();

	void Start( const char* fileName );

	void Stop();
private:
	std::string m_fileName;
};