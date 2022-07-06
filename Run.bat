@echo off 
pushd "%~dp0..\..\" 
call "ParaEngineClient.exe" single="false" mc="true" noupdate="true" dev="%~dp0" mod="MovieCodecPlugin" isDevEnv="true"  
popd 
