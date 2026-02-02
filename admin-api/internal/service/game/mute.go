package game

import "fmt"

// MutePlayer 禁言玩家
func (c *Client) MutePlayer(playerID int64, req *MuteRequest) (*MuteResponse, error) {
	url := fmt.Sprintf("%s/api/players/%d/mute", c.baseURL, playerID)
	var resp MuteResponse
	err := c.doPlayerAction(url, req, &resp)
	return &resp, err
}

// MuteRequest 禁言请求
type MuteRequest struct {
	Duration   int    `json:"duration"`
	Reason     string `json:"reason"`
	Operator   string `json:"operator"`
	OperatorID int    `json:"operator_id"`
}

// MuteResponse 禁言响应
type MuteResponse struct {
	PlayerID   int64  `json:"player_id"`
	PlayerName string `json:"player_name"`
	MuteUntil  string `json:"mute_until"`
}
