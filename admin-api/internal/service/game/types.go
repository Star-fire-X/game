package game

import "time"

// OnlinePlayer 在线玩家信息
type OnlinePlayer struct {
	ID             int64      `json:"id"`
	AccountID      int64      `json:"account_id"`
	Name           string     `json:"name"`
	Level          int        `json:"level"`
	Class          int        `json:"class"`
	ClassName      string     `json:"class_name"`
	Gender         int        `json:"gender"`
	MapID          int        `json:"map_id"`
	MapName        string     `json:"map_name"`
	X              int        `json:"x"`
	Y              int        `json:"y"`
	HP             int        `json:"hp"`
	MaxHP          int        `json:"max_hp"`
	MP             int        `json:"mp"`
	MaxMP          int        `json:"max_mp"`
	LoginTime      time.Time  `json:"login_time"`
	OnlineDuration int        `json:"online_duration"`
	IPAddress      string     `json:"ip_address"`
	IsMuted        bool       `json:"is_muted"`
	MuteUntil      *time.Time `json:"mute_until"`
}

// OnlinePlayersResponse 在线玩家列表响应
type OnlinePlayersResponse struct {
	Total    int64          `json:"total"`
	Page     int            `json:"page"`
	PageSize int            `json:"page_size"`
	Players  []OnlinePlayer `json:"players"`
}
