package game

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
)

// doPlayerAction 执行玩家操作的通用方法
func (c *Client) doPlayerAction(url string, reqBody interface{}, result interface{}) error {
	body, err := json.Marshal(reqBody)
	if err != nil {
		return fmt.Errorf("marshal request failed: %w", err)
	}

	req, err := http.NewRequest(http.MethodPost, url, bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("create request failed: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	var apiResp APIResponse
	if err := json.NewDecoder(resp.Body).Decode(&apiResp); err != nil {
		return fmt.Errorf("decode response failed: %w", err)
	}

	if apiResp.Code != 0 {
		return fmt.Errorf("api error: %s", apiResp.Message)
	}

	if result != nil && len(apiResp.Data) > 0 {
		if err := json.Unmarshal(apiResp.Data, result); err != nil {
			return fmt.Errorf("unmarshal data failed: %w", err)
		}
	}

	return nil
}
