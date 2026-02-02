package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

type UserRepo struct {
	db *gorm.DB
}

func NewUserRepo() *UserRepo {
	return &UserRepo{db: db}
}

func (r *UserRepo) FindByUsername(username string) (*model.AdminUser, error) {
	var user model.AdminUser
	err := r.db.Where("username = ?", username).First(&user).Error
	if err == gorm.ErrRecordNotFound {
		return nil, nil
	}
	return &user, err
}

func (r *UserRepo) FindByID(id int) (*model.AdminUser, error) {
	var user model.AdminUser
	err := r.db.First(&user, id).Error
	if err == gorm.ErrRecordNotFound {
		return nil, nil
	}
	return &user, err
}
