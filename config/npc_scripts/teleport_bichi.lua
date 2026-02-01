-- teleport_bichi.lua
-- 传送脚本：将玩家传送到比奇城 (地图ID=2, 坐标 120,88)
return function(npc, player)
    npc.say(player, "Teleporting now.")
    npc.teleport(player, 2, 120, 88)
end
