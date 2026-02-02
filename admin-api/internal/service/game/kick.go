package game

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
)

// KickPlayer 踢出玩家
func (c *Client) KickPlayer(playerID int64, req *KickRequest) (*KickResponse, error) {
	url := fmt.Sprintf("%s/api/players/%d/kick", c.baseURL, playerID)
	return c.doPlayerAction(url, req, &KickResponse{})
}

// KickRequest 踢人请求
type KickRequest struct {
	Reason     string `json:"reason"`
	Operator   string `json:"operator"`
	OperatorID int    `json:"operator_id"`
}

// KickResponse 踢人响应
type KickResponse struct {
	PlayerID   int64  `json:"player_id"`
	PlayerName string `json:"player_name"`
	KickedAt   string `json:"kicked_at"`
}
