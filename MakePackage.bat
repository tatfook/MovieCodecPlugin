rem build paracraft mod package

if exist MovieCodecPlugin.zip ( del MovieCodecPlugin.zip )
rmdir release /s /q
mkdir release
mkdir release\Mod
mkdir release\Mod\MovieCodecPlugin

mkdir release\bin64
mkdir release\bin64\Mod
mkdir release\bin64\Mod\MovieCodecPlugin


xcopy Mod\MovieCodecPlugin\*.dll release\Mod\MovieCodecPlugin
xcopy Mod\MovieCodecPlugin\*.lua release\Mod\MovieCodecPlugin
xcopy Mod\MovieCodecPlugin\bin64\*.dll release\bin64\Mod\MovieCodecPlugin
xcopy Mod\MovieCodecPlugin\*.lua release\bin64\Mod\MovieCodecPlugin
if exist release\Mod\MovieCodecPlugin\MovieCodecPlugin_d.dll ( del /f release\Mod\MovieCodecPlugin\MovieCodecPlugin_d.dll )

pushd release
call "..\7z.exe" a ..\MovieCodecPlugin.zip Mod\
call "..\7z.exe" a ..\MovieCodecPlugin.zip bin64\
popd