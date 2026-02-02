package middleware

import (
	"encoding/json"
	"strings"

	"github.com/mir2-cpp/admin-api/internal/model"
)

func extractAction(method, path string) string {
	if strings.Contains(path, "/auth/login") {
		return model.ActionLogin
	}
	if strings.Contains(path, "/auth/logout") {
		return model.ActionLogout
	}
	if strings.Contains(path, "/kick") {
		return model.ActionKickPlayer
	}
	if strings.Contains(path, "/ban") {
		return model.ActionBanPlayer
	}
	if strings.Contains(path, "/unban") {
		return model.ActionUnbanPlayer
	}
	if strings.Contains(path, "/mute") {
		return model.ActionMutePlayer
	}
	return method + " " + path
}

func sanitizeBody(body []byte) json.RawMessage {
	if len(body) == 0 {
		return nil
	}
	var data map[string]interface{}
	if err := json.Unmarshal(body, &data); err != nil {
		return nil
	}
	for key := range data {
		if sensitiveFields.MatchString(key) {
			data[key] = "***"
		}
	}
	result, _ := json.Marshal(data)
	return result
}
