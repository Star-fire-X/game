package game

import "fmt"

// UnmutePlayer 解除禁言
func (c *Client) UnmutePlayer(playerID int64, req *UnmuteRequest) (*UnmuteResponse, error) {
	url := fmt.Sprintf("%s/api/players/%d/unmute", c.baseURL, playerID)
	var resp UnmuteResponse
	err := c.doPlayerAction(url, req, &resp)
	return &resp, err
}

// UnmuteRequest 解除禁言请求
type UnmuteRequest struct {
	Reason     string `json:"reason"`
	Operator   string `json:"operator"`
	OperatorID int    `json:"operator_id"`
}

// UnmuteResponse 解除禁言响应
type UnmuteResponse struct {
	PlayerID   int64  `json:"player_id"`
	PlayerName string `json:"player_name"`
	UnmutedAt  string `json:"unmuted_at"`
}
