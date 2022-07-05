Author: LiXizhi
Company: ParaEngine Co.
Date: 2014.5.1

---++ Install
	- download and copy precompiled ffmpeg to Client/trunk/ffmpeg20140501Compiled/ folder. I just do not want to bother building with minGW, and use the one on http://ffmpeg.zeranoe.com/builds/

---++ Build
	[Deprecated:I have added /SAFESEH:NO to cmake] besides CMAKE, one needs to manually Disabling option "Image has Safe Exception Handlers" in Project properties -> Configuration Properties -> Linker -> Advanced tab for all configurations. and build with XP support. 
	otherwise, one will get the "error LNK2026: module unsafe for SAFESEH image." because the ffmpeg lib is not build with the same compiler. 

---++ Deployment Files
	Copy MovieCodecPlugin.zip to Mod/ folder or paracraft and enable it.

---++ Architecture
	- It implements the IMovieSystem.h interface and links dynamically with the ffmpeg (avcodec) library. avcodec is super large and support almost every codec on the earth. 
	- audio capture uses WSAPI which only supports windows 7 or up. 
	- GDI is used for video capture, which is very fast even with high resolution actually. much faster than copy directX backbuffer. 
	- a seperate thread is used for both video and audio capture. 

---++ Changes
2015.3.4
	- audio capturing and encoding are in separate thread now as video. 
	- audio and video encoding share the same thread, since avLib is not thread safe and we need to serialize av_write_frame API for file writing. 
	- since all frames are in order, we use av_write_frame() instead of av_interleaved_write_frame();
	- increase video cache to 30 frames. 
	- encoder will drop FPS in half first when heavy encoding job can not be completed in 30 frames cache. 

2015.2.19  
	- use C++ 11 for threading
	- video encoding and io is moved to another thread. hence there are two threads working, one for graping screen data, the other for encoding. there is a cache of 15 frames in between. 

2019.4
	- 1080p output supported by github.com/sleepingbuffer