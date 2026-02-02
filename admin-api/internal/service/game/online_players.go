package game

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
)

// GetOnlinePlayers 获取在线玩家列表
func (c *Client) GetOnlinePlayers(params OnlinePlayersParams) (*OnlinePlayersResponse, error) {
	u, _ := url.Parse(c.baseURL + "/api/players/online")
	q := u.Query()
	q.Set("page", strconv.Itoa(params.Page))
	q.Set("page_size", strconv.Itoa(params.PageSize))
	if params.Keyword != "" {
		q.Set("keyword", params.Keyword)
	}
	if params.MinLevel > 0 {
		q.Set("min_level", strconv.Itoa(params.MinLevel))
	}
	if params.MaxLevel > 0 {
		q.Set("max_level", strconv.Itoa(params.MaxLevel))
	}
	if params.MapID > 0 {
		q.Set("map_id", strconv.Itoa(params.MapID))
	}
	u.RawQuery = q.Encode()

	resp, err := c.httpClient.Get(u.String())
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("unexpected status: %d", resp.StatusCode)
	}

	var apiResp APIResponse
	if err := json.NewDecoder(resp.Body).Decode(&apiResp); err != nil {
		return nil, fmt.Errorf("decode response failed: %w", err)
	}

	var result OnlinePlayersResponse
	if err := json.Unmarshal(apiResp.Data, &result); err != nil {
		return nil, fmt.Errorf("unmarshal data failed: %w", err)
	}

	return &result, nil
}

// OnlinePlayersParams 查询参数
type OnlinePlayersParams struct {
	Page     int
	PageSize int
	Keyword  string
	MinLevel int
	MaxLevel int
	MapID    int
}
