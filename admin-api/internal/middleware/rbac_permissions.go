package middleware

// Permission definitions
type Permission string

const (
	PermServiceView    Permission = "service:view"
	PermServiceRestart Permission = "service:restart"
	PermServiceStop    Permission = "service:stop"
	PermAlertView      Permission = "alert:view"
	PermAlertConfig    Permission = "alert:config"
	PermPlayerView     Permission = "player:view"
	PermPlayerAction   Permission = "player:action"
	PermAuditView      Permission = "audit:view"
	PermUserManage     Permission = "user:manage"
)

// Permission matrix based on PRD
var rolePermissions = map[string][]Permission{
	RoleOwner: {
		PermServiceView, PermServiceRestart, PermServiceStop,
		PermAlertView, PermAlertConfig,
		PermPlayerView, PermPlayerAction,
		PermAuditView, PermUserManage,
	},
	RoleTechGM: {
		PermServiceView, PermServiceRestart,
		PermAlertView,
		PermPlayerView, PermPlayerAction,
		PermAuditView,
	},
	RoleCS: {
		PermServiceView,
		PermPlayerView, PermPlayerAction,
	},
}
