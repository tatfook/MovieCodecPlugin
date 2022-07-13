## MovieCodec Plugin

Author: LiXizhi
Date: 2014.5

### How To Build
Git clone to Mod/ folder.

Add environment variable `NPLRUNTIME_ROOT` that point to git root of NPLRuntime.

Open this folder with visual studio 2017 or above and build the cmake file.
The output is `Mod/MovieCodecPlugin/MovieCodecPlugin.dll`, which has no external dependencies.

Run `MakePackage.bat` which will generate the `MovieCodecPlugin.zip` paracraft mod installer file.

### How to Build FFMPEG with VCPKG
features: x264,mp3lame,fdk-aac,nonfree 
```
vcpkg install ffmpeg[x264,mp3lame,fdk-aac,nonfree]:x86-windows-static
```
This file can be copied to `Mod/MovieCodecPlugin.zip` to install it manually to paracraft.


### How to Build FFMPEG From Source (Not recommended)
currently only FFMPEG 3.4.X is supported, which is released in 2018. There are some breaking API changes since FFMPEG 4.0. 
Building FFMPEG static library with visual studio is kind of complicated. 
follow the offical document, here are some highlights:

1. install msys2
1. install yasm
1. run msys2 shell, and add visual studio cl/link folder to PATH with 
open command line from visual studio, then open msys2_shell, which will inherit PATH variable. 
```
export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.30.30705/bin/Hostx86/x86/":$PATH
```
1. install pkg-config and diff tools in msys2
```
pacman -S pkgconf
pacman -S diffutils
```
1. run configure in msys2 shell
```
./configure --toolchain=msvc --enable-gpl --enable-nonfree
make
make install
```
external libraries are required in order to compile with `--enable-libx264 --enable-libmp3lame`.

### How to Debug
Run "Run.bat" to start debugging the dll.
One can also set visual studio's project command line property to match the ones in the bat file like below
```
command: C:\lxzsrc\ParaEngine\ParaWorld\paraengineclient.exe
command args: dev="C:\lxzsrc\ParaEngine\ParaWorld\Mod\MovieCodecPlugin" mc="true" mod="MovieCodecPlugin" isDevEnv="true"  world="worlds/DesignHouse/TestLiveModel"
```

### How to Install On Win32
Open paracraft's mod site and install MovieCodecPlugin, make sure it is enabled by default.

### How to Run
Press F9 or use `/record` command to launch the movie codec.

![image](https://user-images.githubusercontent.com/94537/28371691-412a602c-6cd0-11e7-8fb5-18e11be2d4d8.png)

## 安装指南
- 使用Paracraft下载MOD
- 在Paracraft中按F9或输入`/record`命令启动插件。

## 常见问题
- 请确保电脑使用的是立体声输出，否则会crash.
