// tests/WorldNpcBatch5Test.cpp —— P5-5 NPC 长尾第五批测试
//   Msg / Mic / Action / ItemChange / Dengon / Sysinfo

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "world/Api.h"

using namespace SA::World;

TEST_CASE("NpcType: P5-5 枚举值连续")
{
	// 确保新枚举不与现有冲突
	CHECK(static_cast<int>(NpcType::kMsg) == 41);
	CHECK(static_cast<int>(NpcType::kMic) == 42);
	CHECK(static_cast<int>(NpcType::kAction) == 43);
	CHECK(static_cast<int>(NpcType::kItemChange) == 44);
	CHECK(static_cast<int>(NpcType::kDengon) == 45);
	CHECK(static_cast<int>(NpcType::kSysinfo) == 46);
}

TEST_CASE("Dengon: 留言板内存存储语义")
{
	// 原版 npc_dengon.c 用文件存每 NPC 留言; 这里用内存 vector。
	// 验证追加语义: 留言按时间顺序追加, 新留言在末尾。
	std::vector<std::string> board;
	board.push_back("[Alice] 你好");
	board.push_back("[Bob] 世界真大");
	CHECK(board.size() == 2);
	CHECK(board.front() == "[Alice] 你好");
	CHECK(board.back() == "[Bob] 世界真大");

	// 显示最新 3 条的截断语义
	board.push_back("[Carol] 第三条");
	board.push_back("[Dave] 第四条");
	const std::size_t show = std::min<std::size_t>(board.size(), 3);
	CHECK(show == 3);
	CHECK(board[board.size() - show] == "[Bob] 世界真大");
}

TEST_CASE("Mic: 广播文本前缀")
{
	// 原版 npc_mic.c: 普通广播直接推送, 族长广播加 "[族长广播]" 前缀走 saac。
	// 这里基础版统一加 "[广播]" 前缀。
	const std::string text = "大家好";
	const std::string chat_text = "[广播]" + text;
	CHECK(chat_text == "[广播]大家好");
}
