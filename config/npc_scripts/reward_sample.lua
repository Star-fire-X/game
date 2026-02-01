-- reward_sample.lua
-- 奖励示例脚本：给予玩家物品并扣除金币
return function(npc, player)
    npc.giveItem(player, 3001, 1)
    npc.takeGold(player, 500)
end
