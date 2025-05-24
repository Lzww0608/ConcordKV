package raft

import (
	"fmt"
	"time"
)

// HandleVoteRequest 处理投票请求
func (n *Node) HandleVoteRequest(req *VoteRequest) *VoteResponse {
	n.mu.Lock()
	defer n.mu.Unlock()

	currentTerm := n.getCurrentTerm()

	n.logger.Printf("收到来自 %s 的投票请求，任期: %d", req.CandidateID, req.Term)

	// 1. 如果候选人任期小于当前任期，拒绝投票
	if req.Term < currentTerm {
		n.logger.Printf("拒绝投票：候选人任期 %d 小于当前任期 %d", req.Term, currentTerm)
		return &VoteResponse{
			Term:        currentTerm,
			VoteGranted: false,
		}
	}

	// 2. 如果候选人任期大于当前任期，转为跟随者
	if req.Term > currentTerm {
		n.logger.Printf("收到更高任期 %d，转为跟随者", req.Term)
		n.becomeFollower(req.Term, "")
		currentTerm = req.Term
	}

	// 3. 检查投票条件
	votedFor := n.getVotedFor()

	// 如果已经投票给其他候选人，拒绝投票
	if votedFor != "" && votedFor != req.CandidateID {
		n.logger.Printf("拒绝投票：已投票给 %s", votedFor)
		return &VoteResponse{
			Term:        currentTerm,
			VoteGranted: false,
		}
	}

	// 4. 检查候选人日志是否至少和自己一样新
	lastLogIndex := n.storage.GetLastLogIndex()
	lastLogTerm := n.storage.GetLastLogTerm()

	logUpToDate := false
	if req.LastLogTerm > lastLogTerm {
		logUpToDate = true
	} else if req.LastLogTerm == lastLogTerm && req.LastLogIndex >= lastLogIndex {
		logUpToDate = true
	}

	if !logUpToDate {
		n.logger.Printf("拒绝投票：候选人日志不够新 (候选人: term=%d, index=%d; 自己: term=%d, index=%d)",
			req.LastLogTerm, req.LastLogIndex, lastLogTerm, lastLogIndex)
		return &VoteResponse{
			Term:        currentTerm,
			VoteGranted: false,
		}
	}

	// 5. 投票给候选人
	if err := n.setVotedFor(req.CandidateID); err != nil {
		n.logger.Printf("保存投票状态失败: %v", err)
		return &VoteResponse{
			Term:        currentTerm,
			VoteGranted: false,
		}
	}

	// 重置选举定时器（收到了有效的候选人请求）
	n.resetElectionTimer()

	n.logger.Printf("投票给候选人 %s", req.CandidateID)

	return &VoteResponse{
		Term:        currentTerm,
		VoteGranted: true,
	}
}

// HandleAppendEntries 处理追加日志请求
func (n *Node) HandleAppendEntries(req *AppendEntriesRequest) *AppendEntriesResponse {
	n.mu.Lock()
	defer n.mu.Unlock()

	currentTerm := n.getCurrentTerm()

	n.logger.Printf("收到来自 %s 的追加日志请求，任期: %d，条目数: %d",
		req.LeaderID, req.Term, len(req.Entries))

	// 1. 如果领导者任期小于当前任期，拒绝请求
	if req.Term < currentTerm {
		n.logger.Printf("拒绝追加日志：领导者任期 %d 小于当前任期 %d", req.Term, currentTerm)
		return &AppendEntriesResponse{
			Term:    currentTerm,
			Success: false,
		}
	}

	// 2. 如果领导者任期大于等于当前任期，转为跟随者
	if req.Term >= currentTerm {
		n.becomeFollower(req.Term, req.LeaderID)
	}

	// 重置选举定时器（收到了有效的领导者心跳）
	n.resetElectionTimer()

	// 3. 检查日志一致性
	lastLogIndex := n.storage.GetLastLogIndex()

	// 如果 prevLogIndex 大于本地最后一个日志索引，拒绝
	if req.PrevLogIndex > lastLogIndex {
		n.logger.Printf("日志不一致：prevLogIndex %d 大于 lastLogIndex %d", req.PrevLogIndex, lastLogIndex)
		return &AppendEntriesResponse{
			Term:          req.Term,
			Success:       false,
			ConflictIndex: lastLogIndex + 1,
			ConflictTerm:  0,
		}
	}

	// 如果 prevLogIndex > 0，检查 prevLogTerm 是否匹配
	if req.PrevLogIndex > 0 {
		prevEntry, err := n.storage.GetLogEntry(req.PrevLogIndex)
		if err != nil {
			n.logger.Printf("获取前一个日志条目失败: %v", err)
			return &AppendEntriesResponse{
				Term:          req.Term,
				Success:       false,
				ConflictIndex: req.PrevLogIndex,
				ConflictTerm:  0,
			}
		}

		if prevEntry.Term != req.PrevLogTerm {
			n.logger.Printf("日志不一致：prevLogTerm %d != %d", prevEntry.Term, req.PrevLogTerm)

			// 查找冲突任期的第一个索引
			conflictTerm := prevEntry.Term
			conflictIndex := req.PrevLogIndex

			for i := req.PrevLogIndex - 1; i >= 1; i-- {
				entry, err := n.storage.GetLogEntry(i)
				if err != nil || entry.Term != conflictTerm {
					conflictIndex = i + 1
					break
				}
			}

			return &AppendEntriesResponse{
				Term:          req.Term,
				Success:       false,
				ConflictIndex: conflictIndex,
				ConflictTerm:  conflictTerm,
			}
		}
	}

	// 4. 如果有新的日志条目，追加到本地日志
	if len(req.Entries) > 0 {
		// 检查是否有冲突的现有条目
		for i, entry := range req.Entries {
			index := req.PrevLogIndex + LogIndex(i) + 1

			if index <= lastLogIndex {
				existingEntry, err := n.storage.GetLogEntry(index)
				if err == nil && existingEntry.Term != entry.Term {
					// 发现冲突，删除这个索引及之后的所有条目
					n.logger.Printf("发现日志冲突在索引 %d，删除后续条目", index)
					if err := n.storage.TruncateLog(index - 1); err != nil {
						n.logger.Printf("截断日志失败: %v", err)
						return &AppendEntriesResponse{
							Term:    req.Term,
							Success: false,
						}
					}
					break
				}
			}
		}

		// 保存新的日志条目
		if err := n.storage.SaveLogEntries(req.Entries); err != nil {
			n.logger.Printf("保存日志条目失败: %v", err)
			return &AppendEntriesResponse{
				Term:    req.Term,
				Success: false,
			}
		}

		n.logger.Printf("成功追加 %d 个日志条目", len(req.Entries))
	}

	// 5. 更新提交索引
	if req.LeaderCommit > n.commitIndex {
		oldCommitIndex := n.commitIndex
		n.commitIndex = min(req.LeaderCommit, n.storage.GetLastLogIndex())

		if n.commitIndex > oldCommitIndex {
			n.logger.Printf("更新 commitIndex 从 %d 到 %d", oldCommitIndex, n.commitIndex)
			// 异步应用已提交的日志
			go n.applyCommittedLogs()
		}
	}

	n.updateMetrics()

	return &AppendEntriesResponse{
		Term:    req.Term,
		Success: true,
	}
}

// HandleInstallSnapshot 处理安装快照请求
func (n *Node) HandleInstallSnapshot(req *InstallSnapshotRequest) *InstallSnapshotResponse {
	n.mu.Lock()
	defer n.mu.Unlock()

	currentTerm := n.getCurrentTerm()

	n.logger.Printf("收到来自 %s 的安装快照请求，任期: %d", req.LeaderID, req.Term)

	// 1. 如果领导者任期小于当前任期，拒绝请求
	if req.Term < currentTerm {
		n.logger.Printf("拒绝安装快照：领导者任期 %d 小于当前任期 %d", req.Term, currentTerm)
		return &InstallSnapshotResponse{
			Term: currentTerm,
		}
	}

	// 2. 转为跟随者
	if req.Term >= currentTerm {
		n.becomeFollower(req.Term, req.LeaderID)
	}

	// 重置选举定时器
	n.resetElectionTimer()

	// 3. 如果这是第一个块，创建新的快照
	if req.Offset == 0 {
		n.logger.Printf("开始接收快照，lastIncludedIndex: %d, lastIncludedTerm: %d",
			req.LastIncludedIndex, req.LastIncludedTerm)
		// 这里应该初始化快照接收状态
		// 为了简化，我们假设快照是一次性发送的
	}

	// 4. 如果是最后一个块，安装快照
	if req.Done {
		snapshot := &Snapshot{
			LastIncludedIndex: req.LastIncludedIndex,
			LastIncludedTerm:  req.LastIncludedTerm,
			Data:              req.Data,
		}

		// 保存快照
		if err := n.storage.SaveSnapshot(snapshot); err != nil {
			n.logger.Printf("保存快照失败: %v", err)
			return &InstallSnapshotResponse{
				Term: req.Term,
			}
		}

		// 恢复状态机
		if err := n.stateMachine.RestoreSnapshot(snapshot.Data); err != nil {
			n.logger.Printf("恢复状态机快照失败: %v", err)
			return &InstallSnapshotResponse{
				Term: req.Term,
			}
		}

		// 更新状态
		n.commitIndex = req.LastIncludedIndex
		n.lastApplied = req.LastIncludedIndex

		// 截断日志（删除快照包含的条目）
		if err := n.storage.TruncateLog(req.LastIncludedIndex); err != nil {
			n.logger.Printf("截断日志失败: %v", err)
		}

		n.logger.Printf("成功安装快照，commitIndex: %d, lastApplied: %d",
			n.commitIndex, n.lastApplied)

		n.updateMetrics()
	}

	return &InstallSnapshotResponse{
		Term: req.Term,
	}
}

// Propose 提议新的日志条目（仅限领导者）
func (n *Node) Propose(data []byte) error {
	n.mu.Lock()
	defer n.mu.Unlock()

	if n.state != Leader {
		return ErrNotLeader
	}

	// 创建新的日志条目
	entry := &LogEntry{
		Index:     n.storage.GetLastLogIndex() + 1,
		Term:      n.getCurrentTerm(),
		Timestamp: time.Now(),
		Type:      EntryNormal,
		Data:      data,
	}

	// 保存到本地日志
	if err := n.storage.SaveLogEntries([]LogEntry{*entry}); err != nil {
		return err
	}

	n.logger.Printf("提议新的日志条目，索引: %d", entry.Index)

	// 立即复制到跟随者
	go n.sendHeartbeats()

	return nil
}

// min 返回两个值中的较小值
func min(a, b LogIndex) LogIndex {
	if a < b {
		return a
	}
	return b
}

// 错误定义
var (
	ErrNotLeader = fmt.Errorf("不是领导者")
)
