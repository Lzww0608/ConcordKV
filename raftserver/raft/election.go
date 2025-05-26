package raft

import (
	"context"
	"sync"
	"time"
)

// startElection 开始选举过程
func (n *Node) startElection() {
	n.mu.RLock()
	currentTerm := n.getCurrentTerm()
	lastLogIndex := n.storage.GetLastLogIndex()
	lastLogTerm := n.storage.GetLastLogTerm()
	servers := n.config.Servers
	n.mu.RUnlock()

	n.logger.Printf("=== 开始选举调试信息 ===")
	n.logger.Printf("当前任期: %d", currentTerm)
	n.logger.Printf("服务器列表长度: %d", len(servers))
	for i, server := range servers {
		n.logger.Printf("服务器[%d]: ID=%s, Address=%s", i, server.ID, server.Address)
	}
	n.logger.Printf("=== 选举调试信息结束 ===")

	n.logger.Printf("开始选举，任期: %d，服务器列表: %+v", currentTerm, servers)

	// 创建投票请求
	req := &VoteRequest{
		Term:         currentTerm,
		CandidateID:  n.id,
		LastLogIndex: lastLogIndex,
		LastLogTerm:  lastLogTerm,
	}

	// 投票计数
	voteCount := 1 // 自己投票给自己
	majority := len(servers)/2 + 1

	n.logger.Printf("开始选举投票，集群大小: %d，需要票数: %d", len(servers), majority)

	// 检查是否为单节点集群
	if len(servers) == 1 {
		n.logger.Printf("单节点集群，直接成为领导者")
		n.becomeLeader()
		return
	}

	// 并发发送投票请求
	var wg sync.WaitGroup
	var mu sync.Mutex

	for _, server := range servers {
		if server.ID == n.id {
			continue // 跳过自己
		}

		wg.Add(1)
		go func(serverID NodeID) {
			defer wg.Done()

			ctx, cancel := context.WithTimeout(n.ctx, time.Second*2)
			defer cancel()

			resp, err := n.transport.SendVoteRequest(ctx, serverID, req)
			if err != nil {
				n.logger.Printf("发送投票请求到 %s 失败: %v", serverID, err)
				return
			}

			mu.Lock()
			defer mu.Unlock()

			// 检查响应任期
			if resp.Term > currentTerm {
				n.logger.Printf("收到更高任期 %d，转为跟随者", resp.Term)
				n.becomeFollower(resp.Term, "")
				return
			}

			// 统计投票
			if resp.VoteGranted {
				voteCount++
				n.logger.Printf("收到来自 %s 的投票，当前票数: %d/%d", serverID, voteCount, majority)

				// 检查是否获得多数票
				if voteCount >= majority {
					// 再次检查状态，确保仍然是候选人
					n.mu.RLock()
					stillCandidate := n.state == Candidate && n.getCurrentTerm() == currentTerm
					n.mu.RUnlock()

					if stillCandidate {
						n.logger.Printf("获得多数票 (%d/%d)，成为领导者", voteCount, majority)
						n.becomeLeader()
					}
				}
			} else {
				n.logger.Printf("来自 %s 的投票被拒绝", serverID)
			}
		}(server.ID)
	}

	wg.Wait()
}

// sendHeartbeats 发送心跳消息
func (n *Node) sendHeartbeats() {
	n.mu.RLock()
	if n.state != Leader {
		n.mu.RUnlock()
		return
	}

	currentTerm := n.getCurrentTerm()
	servers := n.config.Servers
	commitIndex := n.commitIndex
	n.mu.RUnlock()

	n.logger.Printf("发送心跳，任期: %d", currentTerm)

	// 并发发送心跳到所有跟随者
	var wg sync.WaitGroup

	for _, server := range servers {
		if server.ID == n.id {
			continue // 跳过自己
		}

		wg.Add(1)
		go func(serverID NodeID) {
			defer wg.Done()
			n.sendAppendEntriesToFollower(serverID, currentTerm, commitIndex)
		}(server.ID)
	}

	wg.Wait()
}

// sendAppendEntriesToFollower 向跟随者发送追加日志请求
func (n *Node) sendAppendEntriesToFollower(followerID NodeID, term Term, leaderCommit LogIndex) {
	n.mu.RLock()
	nextIndex := n.nextIndex[followerID]
	n.mu.RUnlock()

	// 获取前一个日志条目信息
	var prevLogIndex LogIndex
	var prevLogTerm Term

	if nextIndex > 1 {
		prevLogIndex = nextIndex - 1
		if entry, err := n.storage.GetLogEntry(prevLogIndex); err == nil {
			prevLogTerm = entry.Term
		} else {
			n.logger.Printf("获取日志条目 %d 失败: %v", prevLogIndex, err)
			return
		}
	}

	// 获取要发送的日志条目
	var entries []LogEntry
	lastLogIndex := n.storage.GetLastLogIndex()

	if nextIndex <= lastLogIndex {
		// 计算要发送的条目数量（限制在配置的最大值内）
		endIndex := nextIndex + LogIndex(n.config.MaxLogEntries) - 1
		if endIndex > lastLogIndex {
			endIndex = lastLogIndex
		}

		logEntries, err := n.storage.GetLogEntries(nextIndex, endIndex)
		if err != nil {
			n.logger.Printf("获取日志条目 [%d:%d] 失败: %v", nextIndex, endIndex, err)
			return
		}
		entries = logEntries
	}

	// 创建追加日志请求
	req := &AppendEntriesRequest{
		Term:         term,
		LeaderID:     n.id,
		PrevLogIndex: prevLogIndex,
		PrevLogTerm:  prevLogTerm,
		Entries:      entries,
		LeaderCommit: leaderCommit,
	}

	ctx, cancel := context.WithTimeout(n.ctx, time.Second*5)
	defer cancel()

	resp, err := n.transport.SendAppendEntries(ctx, followerID, req)
	if err != nil {
		n.logger.Printf("发送追加日志到 %s 失败: %v", followerID, err)
		return
	}

	// 处理响应
	n.handleAppendEntriesResponse(followerID, req, resp)
}

// handleAppendEntriesResponse 处理追加日志响应
func (n *Node) handleAppendEntriesResponse(followerID NodeID, req *AppendEntriesRequest, resp *AppendEntriesResponse) {
	n.mu.Lock()
	defer n.mu.Unlock()

	// 检查是否仍然是领导者且任期一致
	if n.state != Leader || n.getCurrentTerm() != req.Term {
		return
	}

	// 检查响应任期
	if resp.Term > req.Term {
		n.logger.Printf("收到更高任期 %d，转为跟随者", resp.Term)
		n.becomeFollower(resp.Term, "")
		return
	}

	if resp.Success {
		// 成功追加日志
		if len(req.Entries) > 0 {
			// 更新 nextIndex 和 matchIndex
			newMatchIndex := req.PrevLogIndex + LogIndex(len(req.Entries))
			n.matchIndex[followerID] = newMatchIndex
			n.nextIndex[followerID] = newMatchIndex + 1

			n.logger.Printf("成功向 %s 复制日志，matchIndex: %d", followerID, newMatchIndex)

			// 尝试推进 commitIndex
			n.tryAdvanceCommitIndex()
		}
	} else {
		// 日志不一致，需要回退
		if resp.ConflictTerm != 0 {
			// 使用冲突优化
			conflictIndex := n.findConflictIndex(resp.ConflictTerm, resp.ConflictIndex)
			n.nextIndex[followerID] = conflictIndex
		} else {
			// 简单回退
			if n.nextIndex[followerID] > 1 {
				n.nextIndex[followerID]--
			}
		}

		n.logger.Printf("日志不一致，回退 %s 的 nextIndex 到 %d", followerID, n.nextIndex[followerID])
	}
}

// tryAdvanceCommitIndex 尝试推进提交索引
func (n *Node) tryAdvanceCommitIndex() {
	lastLogIndex := n.storage.GetLastLogIndex()

	// 从最后一个日志索引开始，向前检查每个索引
	for index := lastLogIndex; index > n.commitIndex; index-- {
		// 检查这个索引的日志条目是否在当前任期创建
		entry, err := n.storage.GetLogEntry(index)
		if err != nil {
			continue
		}

		if entry.Term != n.getCurrentTerm() {
			continue // 只能提交当前任期的日志
		}

		// 统计已复制此索引的节点数量
		replicatedCount := 1 // 领导者自己
		for _, serverID := range n.getFollowerIDs() {
			if n.matchIndex[serverID] >= index {
				replicatedCount++
			}
		}

		// 检查是否达到多数
		majority := len(n.config.Servers)/2 + 1
		if replicatedCount >= majority {
			// 可以安全提交
			n.commitIndex = index
			n.logger.Printf("推进 commitIndex 到 %d", index)

			// 应用已提交的日志
			go n.applyCommittedLogs()
			break
		}
	}
}

// applyCommittedLogs 应用已提交的日志到状态机
func (n *Node) applyCommittedLogs() {
	n.mu.Lock()
	commitIndex := n.commitIndex
	lastApplied := n.lastApplied
	n.mu.Unlock()

	for index := lastApplied + 1; index <= commitIndex; index++ {
		entry, err := n.storage.GetLogEntry(index)
		if err != nil {
			n.logger.Printf("获取日志条目 %d 失败: %v", index, err)
			break
		}

		if err := n.stateMachine.Apply(entry); err != nil {
			n.logger.Printf("应用日志条目 %d 到状态机失败: %v", index, err)
			break
		}

		n.mu.Lock()
		n.lastApplied = index
		n.mu.Unlock()

		n.logger.Printf("成功应用日志条目 %d 到状态机", index)
	}

	n.updateMetrics()
}

// findConflictIndex 查找冲突索引（用于快速回退优化）
func (n *Node) findConflictIndex(conflictTerm Term, conflictIndex LogIndex) LogIndex {
	// 在领导者的日志中查找冲突任期的最后一个条目
	for index := conflictIndex; index >= 1; index-- {
		entry, err := n.storage.GetLogEntry(index)
		if err != nil {
			continue
		}

		if entry.Term == conflictTerm {
			return index + 1 // 返回冲突任期后的第一个索引
		}

		if entry.Term < conflictTerm {
			break // 已经找到更小的任期，停止搜索
		}
	}

	return conflictIndex
}

// getFollowerIDs 获取所有跟随者ID
func (n *Node) getFollowerIDs() []NodeID {
	var followers []NodeID
	for _, server := range n.config.Servers {
		if server.ID != n.id {
			followers = append(followers, server.ID)
		}
	}
	return followers
}
