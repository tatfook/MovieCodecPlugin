rem build paracraft mod package

if exist MovieCodecPlugin.zip ( del MovieCodecPlugin.zip )
rmdir release /s /q
mkdir release
mkdir release\Mod
mkdir release\Mod\MovieCodecPlugin


xcopy Mod\MovieCodecPlugin\*.dll release\Mod\MovieCodecPlugin
if exist release\Mod\MovieCodecPlugin\MovieCodecPlugin_d.dll ( del /f release\Mod\MovieCodecPlugin\MovieCodecPlugin_d.dll )

pushd release
call "..\7z.exe" a ..\MovieCodecPlugin.zip Mod\
popd