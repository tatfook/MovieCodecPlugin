--[[
Title: 
Author(s):  
Date: 
Desc: 
use the lib:
------------------------------------------------------------
NPL.load("(gl)Mod/MovieCodecPlugin/main.lua");
local MovieCodecPlugin = commonlib.gettable("Mod.MovieCodecPlugin");
------------------------------------------------------------
]]
local MovieCodecPlugin = commonlib.inherit(commonlib.gettable("Mod.ModBase"),commonlib.gettable("Mod.MovieCodecPlugin"));

function MovieCodecPlugin:ctor()
end

-- virtual function get mod name

function MovieCodecPlugin:GetName()
	return "MovieCodecPlugin"
end

function MovieCodecPlugin:GetVersion()
	return 10;
end

-- virtual function get mod description 
function MovieCodecPlugin:GetDesc()
	return "MovieCodecPlugin is a plugin in paracraft"
end

function MovieCodecPlugin:init()
	LOG.std(nil, "info", "MovieCodecPlugin", "plugin initialized");
end

function MovieCodecPlugin:OnLogin()
end
-- called when a new world is loaded. 

function MovieCodecPlugin:OnWorldLoad()
end
-- called when a world is unloaded. 

function MovieCodecPlugin:OnLeaveWorld()
end

function MovieCodecPlugin:OnDestroy()
end
