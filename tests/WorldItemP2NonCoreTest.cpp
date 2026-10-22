// tests/WorldItemP2NonCoreTest.cpp —— P2 非核心: 剩余 3 个 usefunc 测试
//   FusionEditBase / LearnRideCode / MaxRedSocks
//
// FusionEditBase (item_event.c:3631): 目标必须是宠物，完整窗口流程待补
// LearnRideCode (item_event.c:3568): 转生门槛 + 按位或写 lowridepets
// MaxRedSocks (item_event.c:2671): ⚠️ seasonal 圣诞道具，时间窗口 [open, open+24h]

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "model/Player.h"
#include "world/Api.h"

using namespace SA::World;
using namespace SA::Model;

// ── ItemEffect 字段存在性 ──

TEST_CASE("ItemEffect: P2 非核心字段")
{
	ItemEffect eff;
	// FusionEditBase
	CHECK(eff.is_fusion_edit_base == false);
	eff.is_fusion_edit_base = true;
	CHECK(eff.is_fusion_edit_base == true);
	// LearnRideCode
	CHECK(eff.is_learn_ride_code == false);
	eff.is_learn_ride_code = true;
	eff.ride_code_index = 3; // RIDE_PET3
	eff.ride_trans_required = 2;
	CHECK(eff.ride_code_index == 3);
	CHECK(eff.ride_trans_required == 2);
	// MaxRedSocks (seasonal)
	CHECK(eff.is_max_red_socks == false);
	eff.is_max_red_socks = true;
	eff.redsocks_open_time = 1760000000;
	CHECK(eff.redsocks_open_time == 1760000000);
}

// ── Player 新增字段 ──

TEST_CASE("Player: 转生与骑乘代码字段")
{
	Player p;
	CHECK(p.transmigration == 0);
	CHECK(p.lowridepets == 0);
	p.transmigration = 3;
	p.lowridepets = 0;
	CHECK(p.transmigration == 3);
	// 模拟学习 RIDE_PET3 (bit 3)
	p.lowridepets |= (1 << 3);
	CHECK(p.lowridepets == 8);
	// 再学 RIDE_PET0 (bit 0)
	p.lowridepets |= (1 << 0);
	CHECK(p.lowridepets == 9);
}

// ── LearnRideCode 转生门槛逻辑 ──
// 原版 item_event.c:3589: if(CHAR_getInt(charaindex, CHAR_TRANSMIGRATION) < ridetrans) 拒绝

TEST_CASE("LearnRideCode: 转生门槛判定")
{
	auto can_learn = [](std::int32_t transmigration,
	                    std::int32_t required) -> bool
	{ return transmigration >= required; };

	CHECK(can_learn(0, 0) == true);  // 无要求
	CHECK(can_learn(2, 2) == true);  // 刚好达标
	CHECK(can_learn(3, 2) == true);  // 超过
	CHECK(can_learn(1, 2) == false); // 不足
	CHECK(can_learn(0, 1) == false); // 0 转学 1 转要求
}

// ── LearnRideCode 位掩码逻辑 ──
// 原版: LRCode = LRCode | NewRides[i].Code (RIDE_PET0..11)

TEST_CASE("LearnRideCode: 位掩码按位或")
{
	std::int32_t code = 0;
	// 学 RIDE_PET0 (bit 0)
	code |= (1 << 0);
	CHECK(code == 1);
	// 学 RIDE_PET11 (bit 11)
	code |= (1 << 11);
	CHECK(code == 2049);
	// 重复学 RIDE_PET0 不变
	code |= (1 << 0);
	CHECK(code == 2049);
}

// ── MaxRedSocks 时间窗口逻辑 ──
// 原版 item_event.c:2690: if(nowtimes >= itemtimes && nowtimes <= itemtimes+(60*60*24))

TEST_CASE("MaxRedSocks: 时间窗口判定 (seasonal)")
{
	constexpr std::int64_t kWindowSecs = 24 * 60 * 60;
	auto in_window = [](std::int64_t now, std::int64_t open) -> bool
	{ return now >= open && now <= open + kWindowSecs; };

	const std::int64_t open = 1760000000;
	CHECK(in_window(open, open) == true);                    // 刚好开启
	CHECK(in_window(open + kWindowSecs, open) == true);      // 窗口末尾
	CHECK(in_window(open + 3600, open) == true);             // 窗口内
	CHECK(in_window(open - 1, open) == false);               // 太早
	CHECK(in_window(open + kWindowSecs + 1, open) == false); // 过期
}

// ── MaxRedSocks 礼物加权随机 ──
// 原版 item_event.c:2697-2703:
//   si = rand()%100; >70 → rand()%3+10 (10-12); >60 → rand()%2+8 (8-9); 否则 rand()%8 (0-7)

TEST_CASE("MaxRedSocks: 礼物索引加权分布")
{
	// present[13] = {13061,13062,13063,13064,13088,13089,13090,13091,
	//                14756,17256,13092,19692,20594}
	auto pick_index = [](int si_roll, int sub_roll) -> int
	{
		if (si_roll > 70)
			return sub_roll % 3 + 10;
		else if (si_roll > 60)
			return sub_roll % 2 + 8;
		else
			return sub_roll % 8;
	};
	// 高档 (10-12)
	CHECK(pick_index(71, 0) == 10);
	CHECK(pick_index(99, 2) == 12);
	// 中档 (8-9)
	CHECK(pick_index(61, 0) == 8);
	CHECK(pick_index(70, 1) == 9);
	// 普通 (0-7)
	CHECK(pick_index(60, 0) == 0);
	CHECK(pick_index(0, 7) == 7);
	// 边界：71 > 70 进高档，70 不进
	CHECK(pick_index(71, 0) >= 10);
	CHECK(pick_index(70, 0) < 10);
}
