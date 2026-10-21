// shared/rules/PetSkill.cpp —— 宠技战斗侧与特殊指令纯函数实现 (批次 A-δ)

#include "rules/PetSkill.h"

#include <algorithm>
#include <utility>

namespace SA::Rules
{

SacrificeResult computeSacrifice(std::int32_t user_hp,
                                 std::int32_t user_max_hp,
                                 std::int32_t target_hp,
                                 std::int32_t target_max_hp) noexcept
{
	SacrificeResult r{};
	// 原版: 施法者 HP 必须 > 最大 HP 20%，否则"自身耐久力不足 救援失败"
	// user_hp * 5 > user_max_hp  ⟺  user_hp > user_max_hp * 0.2（整数运算避免浮点）
	if (user_max_hp <= 0 || user_hp * 5 <= user_max_hp || target_hp <= 0 || target_max_hp <= 0)
		return r;

	// 原版: 施法者新血量 = (int)(HP*0.5)；user_loss = 原血量 - 新血量
	const std::int32_t new_hp = static_cast<std::int32_t>(user_hp * 0.5);
	r.user_loss = user_hp - new_hp;
	if (r.user_loss <= 0)
		r.user_loss = 1;

	std::int32_t heal = r.user_loss;
	if (target_hp + heal > target_max_hp)
		heal = target_max_hp - target_hp;
	if (heal < 0)
		heal = 0;

	r.target_heal = heal;
	return r;
}

std::int32_t computeSelfExplodeDamage(std::int32_t defender_hp) noexcept
{
	if (defender_hp <= 0)
		return 0;
	return defender_hp / 2;
}

bool rollFallGround(bool victim_has_ride, Random &rng, int resist) noexcept
{
	if (!victim_has_ride)
		return false;
	const int threshold = 50 + resist;
	// 原版 battle_event.c:6025: RAND(0,100)（0..100 共 101 个值），非 rand(1,100)
	const int roll = rng.rand(0, 100);
	return roll > threshold;
}

void applyAttributeReverse(Combatant &c) noexcept
{
	// 地(0) 与 火(2) 互换 (battle.c:2888, 2890)
	std::swap(c.elements[0], c.elements[2]);
	// 水(1) 与 风(3) 互换 (battle.c:2889, 2891)
	std::swap(c.elements[1], c.elements[3]);
}

bool isRoarTarget(int target_pet_id, const int *roar_pet_ids, std::size_t roar_count) noexcept
{
	if (target_pet_id <= 0 || roar_pet_ids == nullptr || roar_count == 0)
		return false;
	for (std::size_t i = 0; i < roar_count; ++i)
	{
		if (target_pet_id == roar_pet_ids[i])
			return true;
	}
	return false;
}

bool rollPetStatusSkill(int success_percent, Random &rng) noexcept
{
	if (success_percent <= 0)
		return false;
	if (success_percent >= 100)
		return true;
	return rng.rand(1, 100) <= success_percent;
}

bool isStatusCuredByRefresh(int current_status, int target_cure_status) noexcept
{
	if (current_status <= 0)
		return false;
	if (target_cure_status == 0)
		return true; // 0 代表 "全"
	return current_status == target_cure_status;
}

} // namespace SA::Rules
