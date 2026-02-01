-- quest_intro.lua
-- 任务引导脚本：向玩家介绍任务并开始任务
return function(npc, player)
    npc.say(player, "Brave hero, I have a task for you.")
    npc.startQuest(player, 2001)
end
