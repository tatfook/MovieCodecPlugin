## MovieCodec Plugin

Author: LiXizhi
Date: 2014.5

### How To Build
Git clone to Mod/ folder.

Add environment variable `NPLRUNTIME_ROOT` that point to git root of NPLRuntime.

Open this folder with visual studio 2017 or above and build the cmake file.
The output is `Mod/MovieCodecPlugin/MovieCodecPlugin.dll`, which has no external dependencies.

Run `MakePackage.bat` which will generate the `MovieCodecPlugin.zip` paracraft mod installer file.
This file can be copied to `Mod/MovieCodecPlugin.zip` to install it manually to paracraft.


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
