package game

// CharacterInfo 角色详细信息
type CharacterInfo struct {
	ID            int64  `json:"id"`
	AccountID     int64  `json:"account_id"`
	Name          string `json:"name"`
	Level         int    `json:"level"`
	Class         int    `json:"class"`
	ClassName     string `json:"class_name"`
	Gender        int    `json:"gender"`
	HP            int    `json:"hp"`
	MaxHP         int    `json:"max_hp"`
	MP            int    `json:"mp"`
	MaxMP         int    `json:"max_mp"`
	Attack        int    `json:"attack"`
	Defense       int    `json:"defense"`
	MagicAttack   int    `json:"magic_attack"`
	MagicDefense  int    `json:"magic_defense"`
	Speed         int    `json:"speed"`
	Experience    int64  `json:"experience"`
	Gold          int64  `json:"gold"`
	MapID         int    `json:"map_id"`
	MapName       string `json:"map_name"`
	X             int    `json:"x"`
	Y             int    `json:"y"`
	IsOnline      bool   `json:"is_online"`
	IsBanned      bool   `json:"is_banned"`
	CreatedAt     string `json:"created_at"`
	LastLoginAt   string `json:"last_login_at"`
}
