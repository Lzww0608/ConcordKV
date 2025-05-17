package concord

// 事务隔离级别常量
const (
	// 读未提交：允许脏读，不防止不可重复读和幻读
	IsolationReadUncommitted = "read_uncommitted"

	// 读已提交：防止脏读，但允许不可重复读和幻读
	IsolationReadCommitted = "read_committed"

	// 可重复读：防止脏读和不可重复读，但允许幻读
	IsolationRepeatableRead = "repeatable_read"

	// 串行化：防止所有并发问题（脏读、不可重复读和幻读）
	IsolationSerializable = "serializable"
)
