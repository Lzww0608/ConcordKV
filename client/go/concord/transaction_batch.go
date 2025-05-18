package concord

import (
	"errors"
)

// BatchGet 在事务中批量获取多个键的值
func (t *Transaction) BatchGet(keys []string) (*BatchResult, error) {
	if t.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键
	for _, key := range keys {
		// 获取单个键的值
		value, err := t.Get(key)

		if err != nil {
			result.Succeeded[key] = false
			result.Errors[key] = err
		} else {
			result.Values[key] = value
			result.Succeeded[key] = true
		}
	}

	return result, nil
}

// BatchSet 在事务中批量设置多个键值对
func (t *Transaction) BatchSet(pairs []KeyValue) (*BatchResult, error) {
	if t.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if t.options.ReadOnly {
		return nil, errors.New("只读事务不允许写操作")
	}

	if len(pairs) == 0 {
		return nil, errors.New("键值对列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键值对
	for _, pair := range pairs {
		// 设置单个键值对
		err := t.Set(pair.Key, pair.Value)

		if err != nil {
			result.Succeeded[pair.Key] = false
			result.Errors[pair.Key] = err
		} else {
			result.Succeeded[pair.Key] = true
		}
	}

	return result, nil
}

// BatchDelete 在事务中批量删除多个键
func (t *Transaction) BatchDelete(keys []string) (*BatchResult, error) {
	if t.status != "active" {
		return nil, errors.New("事务已关闭")
	}

	if t.options.ReadOnly {
		return nil, errors.New("只读事务不允许写操作")
	}

	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()

	// 在事务中顺序处理所有键
	for _, key := range keys {
		// 删除单个键
		err := t.Delete(key)

		if err != nil {
			result.Succeeded[key] = false
			result.Errors[key] = err
		} else {
			result.Succeeded[key] = true
		}
	}

	return result, nil
}
