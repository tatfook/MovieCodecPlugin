#pragma once
#include <string>

class AudioRecorder
{
public:
	AudioRecorder();

	~AudioRecorder();

	void Start( const char* fileName );

	void Stop();
private:
	std::string m_fileName;
};