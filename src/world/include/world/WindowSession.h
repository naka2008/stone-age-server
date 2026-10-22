// src/world/include/world/WindowSession.h —— 窗口会话管理器
//
// 取代原版 char.c 的 seqno 全局路由链 (DR-PR3):
//   原版用 160 个 CHAR_WINDOWTYPE_* 常量做回执路由, 其中只有 32 个被实际判定,
//   其余是废弃或 NPC 自用。本实现把状态放在服务端, window_id 只需会话内唯一,
//   不建全局编号空间。每个窗口交互是一个显式状态机, handler 在 open 时注册,
//   回执到达时路由到对应 handler, 处理完即关闭 (一次性语义)。

#ifndef __SA_World_WindowSession_H__
#define __SA_World_WindowSession_H__

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "domain/window.sa.h"
#include "net/Api.h"

namespace SA::World
{

// 窗口会话管理器: (session, window_id) → handler 路由。
class WindowSessionManager
{
  public:
	// 回执处理函数。返回 true 表示已消费该回执 (窗口会话随即关闭);
	// 返回 false 表示未消费, 路由器继续尝试其它派发路径。
	using ReplyHandler = std::function<bool(SA::Net::SessionId, const SA::Domain::WindowReply &)>;

	// 打开一个窗口会话: 分配 window_id (会话内唯一递增, 0 保留为无效)
	// 并注册 handler。返回分配到的 window_id。
	std::uint32_t openWindow(SA::Net::SessionId session, ReplyHandler handler);

	// 路由回执: 若 (session, window_id) 有注册的 handler 则调用之。
	// handler 返回 true 时关闭该窗口会话并返回 true; 否则返回 false。
	bool onReply(SA::Net::SessionId session, const SA::Domain::WindowReply &reply);

	// 主动关闭一个窗口会话 (不触发 handler)。
	void closeWindow(SA::Net::SessionId session, std::uint32_t window_id);

	// 会话断开时清理该会话的全部窗口会话。
	void clearSession(SA::Net::SessionId session);

	// 观察面 (测试): 该会话当前打开的窗口会话数。
	std::size_t sessionWindowCount(SA::Net::SessionId session) const;

  private:
	struct SessionState
	{
		std::uint32_t next_id = 0;
		std::unordered_map<std::uint32_t, ReplyHandler> handlers;
	};

	std::unordered_map<SA::Net::SessionId, SessionState> sessions_;
};

} // namespace SA::World

#endif // __SA_World_WindowSession_H__
