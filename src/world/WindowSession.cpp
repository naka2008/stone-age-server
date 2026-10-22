// src/world/WindowSession.cpp —— 窗口会话管理器实现
//
// 对应原版: gmsv/char.c 的窗口回执 seqno 路由链 (约 5477 行处的 if/else if 链)。
// 原版用全局 CHAR_WINDOWTYPE_* 常量做键; 本实现用 (session, window_id) 做键,
// 状态存在服务端, 不建全局编号空间 (DR-PR3)。

#include "world/WindowSession.h"

namespace SA::World
{

std::uint32_t WindowSessionManager::openWindow(SA::Net::SessionId session, ReplyHandler handler)
{
	SessionState &st = sessions_[session];
	// 会话内唯一递增; 0 保留为无效, 跳过它 (回绕时)
	std::uint32_t id = ++st.next_id;
	if (id == 0)
		id = ++st.next_id;
	st.handlers.emplace(id, std::move(handler));
	return id;
}

bool WindowSessionManager::onReply(SA::Net::SessionId session, const SA::Domain::WindowReply &reply)
{
	const auto sit = sessions_.find(session);
	if (sit == sessions_.end())
		return false;
	SessionState &st = sit->second;
	const auto hit = st.handlers.find(reply.window_id);
	if (hit == st.handlers.end())
		return false;

	// 先取出 handler 再调用: handler 内部可能 open 新窗口 / close 当前窗口。
	// 调用后重新查表 (handler 可能触发外层 map 重哈希, 不能沿用 st 引用)。
	const std::uint32_t wid = reply.window_id;
	ReplyHandler handler = std::move(hit->second);
	st.handlers.erase(hit);

	const bool consumed = handler(session, reply);
	if (!consumed)
	{
		// 未消费 ⇒ 恢复注册, 让其它派发路径继续尝试。
		sessions_[session].handlers.emplace(wid, std::move(handler));
	}
	return consumed;
}

void WindowSessionManager::closeWindow(SA::Net::SessionId session, std::uint32_t window_id)
{
	const auto sit = sessions_.find(session);
	if (sit == sessions_.end())
		return;
	sit->second.handlers.erase(window_id);
}

void WindowSessionManager::clearSession(SA::Net::SessionId session)
{
	sessions_.erase(session);
}

std::size_t WindowSessionManager::sessionWindowCount(SA::Net::SessionId session) const
{
	const auto sit = sessions_.find(session);
	if (sit == sessions_.end())
		return 0;
	return sit->second.handlers.size();
}

} // namespace SA::World
