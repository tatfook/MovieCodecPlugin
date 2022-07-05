# Changelog
All notable changes to this project will be documented in this file.

## 2022.7.5 LiXizhi
I am trying to support AAC audio encoder in mp4, so that audio can be played in iOS. However, ffmepg no longer provide static build shared library files. 

## [1.0.1] - 2018-05-30 Cheng Yuanchu <sleepybuffer@gmail.com>

### Added
- MCI interface implementation which enables Paracaft to record sound from microphone input
- Add mp3 output format to MovieCodecPlugin. Now you can export audio file if you like

### Deatails for Developers
- Add new class AudioRecordAudioFromMicrophone which wraps MCI interface 

## [1.0.0] - 2018-05-14 Cheng Yuanchu <sleepybuffer@gmail.com>

### Added
- 1080p output format is now supported. Currently, 1080p HD mode gives you the best picture available. Its resolution is 1,920 x 1,080 pixels, and all the lines of the image are visible at the same time. Since it's smoother, it does a better job, especially when capturing large scene with a lot stuff to render.
- Stereo sound.

### Details for Developers
- Add new class AudioMixer. AudioMixer merges multi audio streams (.wav, .au etc.) inputs into a single one with proper delay(s). If your audio stream is for example longer than the video stream, you have to cut it or otherwise you will have the last video frame as a still image and audio running. 
- Add static win32 ffmpeg libs in ./x86 folder. We now can release our MovieCodecPlugin with just one DLL file, no prebuild ffmpeg DLL files anymore.
