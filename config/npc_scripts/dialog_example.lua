-- dialog_example.lua
-- 对话树示例脚本：展示多选菜单与分支对话逻辑
return function(npc, player)
    local choice = npc.showMenu(player, {
        "我想购买物品",
        "我想存储物品",
        "告诉我这里的故事"
    })

    if choice == 1 then
        npc.openShop(player, 1001)
    elseif choice == 2 then
        npc.openStorage(player)
    elseif choice == 3 then
        npc.say(player, "This is the legendary city of Bichi...")
    end
end
