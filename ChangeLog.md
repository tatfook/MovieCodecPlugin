# Changelog
All notable changes to this project will be documented in this file.

## [1.0.0] - 2018-05-14 Cheng Yuanchu <sleepybuffer@gmail.com>

### Added
- 1080p output format is now supported. Currently, 1080p HD mode gives you the best picture available. Its resolution is 1,920 x 1,080 pixels, and all the lines of the image are visible at the same time. Since it's smoother, it does a better job, especially when capturing large scene with a lot stuff to render.
- Stereo sound.

### Deatails for Developers
- Add new class AudioMixer. AudioMixer merges multi audio streams (.wav, .au etc.) inputs into a single one with proper delay(s). If your audio stream is for example longer than the video stream, you have to cut it or otherwise you will have the last video frame as a still image and audio running. 
- Add static win32 ffmpeg libs in ./x86 folder. We now can release our MovieCodecPlugin with just one DLL file, no prebuild DLL files anymore.
