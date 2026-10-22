// tests/WindowSessionTest.cpp —— 窗口会话管理器用例
//
// 守的是 DR-PR3 的三条结构性约束:
//   ① window_id 会话内唯一递增 (0 保留为无效);
//   ② onReply 把回执路由到注册的 handler, handler 消费后窗口会话关闭 (一次性语义);
//   ③ 未注册的 window_id / 会话 → onReply 返回 false, 不干扰其它派发路径。

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "WindowSession.h"

using namespace SA::World;

namespace
{

SA::Domain::WindowReply makeReply(std::uint32_t window_id, std::uint32_t button = 1)
{
	SA::Domain::WindowReply r{};
	r.window_id = window_id;
	r.button = button;
	return r;
}

} // namespace

TEST_CASE("openWindow: 会话内唯一递增, 0 保留")
{
	WindowSessionManager mgr;
	const SA::Net::SessionId s1 = 1001;
	const SA::Net::SessionId s2 = 1002;

	const std::uint32_t a1 = mgr.openWindow(s1, [](auto, const auto &)
	                                        { return true; });
	const std::uint32_t a2 = mgr.openWindow(s1, [](auto, const auto &)
	                                        { return true; });
	const std::uint32_t b1 = mgr.openWindow(s2, [](auto, const auto &)
	                                        { return true; });

	CHECK(a1 != 0);
	CHECK(a2 != 0);
	CHECK(a1 != a2); // 同一会话内唯一
	CHECK(b1 != 0);
	// 不同会话各自从 1 开始计数 (会话内唯一, 非全局唯一)
	CHECK(b1 == 1);

	CHECK(mgr.sessionWindowCount(s1) == 2);
	CHECK(mgr.sessionWindowCount(s2) == 1);
	CHECK(mgr.sessionWindowCount(9999) == 0); // 未知会话
}

TEST_CASE("onReply: 路由到 handler, 消费后关闭 (一次性语义)")
{
	WindowSessionManager mgr;
	const SA::Net::SessionId s = 2001;

	bool handler_called = false;
	std::uint32_t got_button = 0;
	const std::uint32_t wid = mgr.openWindow(
	    s, [&](SA::Net::SessionId sid, const SA::Domain::WindowReply &reply)
	    {
		    handler_called = true;
		    got_button = reply.button;
		    CHECK(sid == s);
		    return true; // 消费
	    });

	CHECK(mgr.sessionWindowCount(s) == 1);

	// 第一次回执 → handler 被调用并消费
	const bool routed = mgr.onReply(s, makeReply(wid, 4 /* YES */));
	CHECK(routed);
	CHECK(handler_called);
	CHECK(got_button == 4);
	CHECK(mgr.sessionWindowCount(s) == 0); // 已关闭

	// 第二次用同一 window_id 回执 → 无 handler, 返回 false
	handler_called = false;
	CHECK(!mgr.onReply(s, makeReply(wid, 4)));
	CHECK(!handler_called);
}

TEST_CASE("onReply: 未消费时不关闭, 未知 id 返回 false")
{
	WindowSessionManager mgr;
	const SA::Net::SessionId s = 3001;

	int call_count = 0;
	const std::uint32_t wid = mgr.openWindow(
	    s, [&](auto, const auto &)
	    {
		    ++call_count;
		    return false; // 不消费
	    });

	// handler 返回 false → onReply 返回 false, 窗口会话保留
	CHECK(!mgr.onReply(s, makeReply(wid)));
	CHECK(call_count == 1);
	CHECK(mgr.sessionWindowCount(s) == 1);

	// 未知的 window_id → false, 不影响已有会话
	CHECK(!mgr.onReply(s, makeReply(9999)));
	CHECK(call_count == 1);

	// 未知的会话 → false
	CHECK(!mgr.onReply(8888, makeReply(wid)));
	CHECK(call_count == 1);

	// closeWindow 主动关闭 (不触发 handler)
	mgr.closeWindow(s, wid);
	CHECK(mgr.sessionWindowCount(s) == 0);
	CHECK(!mgr.onReply(s, makeReply(wid)));
	CHECK(call_count == 1);

	// clearSession 清理整个会话
	mgr.openWindow(s, [](auto, const auto &)
	               { return true; });
	mgr.openWindow(s, [](auto, const auto &)
	               { return true; });
	CHECK(mgr.sessionWindowCount(s) == 2);
	mgr.clearSession(s);
	CHECK(mgr.sessionWindowCount(s) == 0);
}
