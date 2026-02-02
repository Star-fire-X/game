package repository

// SearchCharacters 搜索角色
func (r *CharacterRepository) SearchCharacters(keyword string, page, pageSize int) ([]Character, int64, error) {
	var chars []Character
	var total int64

	query := r.db.Model(&Character{})
	if keyword != "" {
		query = query.Where("name ILIKE ?", "%"+keyword+"%")
	}

	query.Count(&total)
	err := query.Order("level DESC").
		Offset((page - 1) * pageSize).
		Limit(pageSize).
		Find(&chars).Error

	return chars, total, err
}

// GetCharacterByID 根据 ID 获取角色
func (r *CharacterRepository) GetCharacterByID(id int64) (*Character, error) {
	var char Character
	err := r.db.First(&char, id).Error
	return &char, err
}

// GetCharacterByName 根据名称获取角色
func (r *CharacterRepository) GetCharacterByName(name string) (*Character, error) {
	var char Character
	err := r.db.Where("name = ?", name).First(&char).Error
	return &char, err
}
