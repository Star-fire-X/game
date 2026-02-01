-- merchant_default.lua
-- 默认商人脚本：向玩家打招呼并打开商店界面
return function(npc, player)
    npc.say(player, "Welcome! Take a look at my goods.")
    npc.openShop(player, 1001)
end
