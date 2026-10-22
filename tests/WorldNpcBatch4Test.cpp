// tests/WorldNpcBatch4Test.cpp —— P5-4 NPC 长尾第四批测试
//   WindowHealer / Janken / Airplane / BodyLan / CheckMan / Windowman / Welfare

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "world/Api.h"

using namespace SA::World;

TEST_CASE("jankenJudge: 胜负判定对齐原版 npc_janken.c")
{
	// 原版 NPC_JnakenJudge 胜负表 (npc_janken.c:220-245)
	//   jankenman=0(石头): 玩家布(2)胜, 剪刀(1)败
	//   jankenman=1(剪刀): 玩家石头(0)胜, 布(2)败
	//   jankenman=2(布): 玩家剪刀(1)胜, 石头(0)败

	// 平局
	CHECK(World::jankenJudge(0, 0) == 0);
	CHECK(World::jankenJudge(1, 1) == 0);
	CHECK(World::jankenJudge(2, 2) == 0);

	// 玩家胜
	CHECK(World::jankenJudge(0, 1) == 1); // 石头胜剪刀
	CHECK(World::jankenJudge(1, 2) == 1); // 剪刀胜布
	CHECK(World::jankenJudge(2, 0) == 1); // 布胜石头

	// NPC胜
	CHECK(World::jankenJudge(1, 0) == 2); // 剪刀败给石头
	CHECK(World::jankenJudge(2, 1) == 2); // 布败给剪刀
	CHECK(World::jankenJudge(0, 2) == 2); // 石头败给布
}

TEST_CASE("NpcType: P5-4 枚举值连续")
{
	// 确保新枚举不与现有冲突
	CHECK(static_cast<int>(NpcType::kWindowHealer) == 34);
	CHECK(static_cast<int>(NpcType::kJanken) == 35);
	CHECK(static_cast<int>(NpcType::kAirplane) == 36);
	CHECK(static_cast<int>(NpcType::kBodyLan) == 37);
	CHECK(static_cast<int>(NpcType::kCheckMan) == 38);
	CHECK(static_cast<int>(NpcType::kWindowman) == 39);
	CHECK(static_cast<int>(NpcType::kWelfare) == 40);
}
