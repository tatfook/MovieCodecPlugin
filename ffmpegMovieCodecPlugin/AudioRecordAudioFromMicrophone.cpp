#include "AudioRecordAudioFromMicrophone.h"

#include <Windows.h>

AudioRecordAudioFromMicrophone::AudioRecordAudioFromMicrophone()
{
}

AudioRecordAudioFromMicrophone::~AudioRecordAudioFromMicrophone()
{

}

#define ALIAS "recsound"
void AudioRecordAudioFromMicrophone::Start(const char* fileName)
{
	char mci_command[100];
	char ReturnString[300];
	int mci_error;
	sprintf(mci_command, "open new Type waveaudio Alias %s", ALIAS);
	mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);

	// set the time format
	//sprintf(mci_command, "set %s time format ms", ALIAS);    // just set time format
	//mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);

	// start recording
	sprintf(mci_command, "record %s", ALIAS);
	mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);
}

void AudioRecordAudioFromMicrophone::Stop()
{
	char mci_command[100];
	char ReturnString[300];
	int mci_error;
	//stop recording
	sprintf(mci_command, "stop %s", ALIAS);
	mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);

	// save the file
	sprintf(mci_command, "save %s %s", ALIAS, "C:/Users/azoth/Desktop/test_recorder.wav");
	mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);

	// close the device
	sprintf(mci_command, "close %s", ALIAS);
	mci_error = mciSendString(mci_command, ReturnString, sizeof(ReturnString), NULL);
}