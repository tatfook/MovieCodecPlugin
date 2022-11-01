@echo off 
pushd "%~dp0..\..\" 
rem call "D:\work\para_client\ParaEngineClient.exe" single="false" mc="true" noupdate="true" dev="%~dp0" mod="MovieCodecPlugin" isDevEnv="true"  
call "D:\work\NPLRuntime\Client\build_nodll\ParaEngineClient\Release\ParaEngineClient.exe" single="false" mc="true" noupdate="true" dev="%~dp0" mod="MovieCodecPlugin" isDevEnv="true"  
rem call "D:\work\NPLRuntime\Client\build_nodll\ParaEngineClient\Debug\ParaEngineClient_d.exe" single="false" mc="true" noupdate="true" dev="%~dp0" mod="MovieCodecPlugin" isDevEnv="true"  

popd 
