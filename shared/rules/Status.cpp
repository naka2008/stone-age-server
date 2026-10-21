// shared/rules/Status.cpp —— L4 状态异常系统的实现(批次 L4.1)
//
// 契约与取证依据见 rules/Status.h 卷首。
//
// ── 宏门控的处置(D7:8.0 单基线,不迁移宏)──────────────────────
//   已逐条回 `StoneAge/gmsv/src/include/version.h` 核实(2026-09-11):
//   `_SUIT_ADDPART4`(带毒装备)· `_SUIT_ADDENDUM`(通用抗性)·
//   `_EQUIT_RESIST`(三种装备抗性)· `_SUIT_ADDPART3`(虚弱第二道)·
//   `_PET_SKILL_SARS`(毒煞)· `_MAGIC_WEAKEN` · `_MAGIC_BARRIER`(计时冻结)·
//   `_MAGIC_DEEPPOISON`(剧毒直死)· `_PROFESSION_SKILL` · `_PROFESSION_ADDSKILL`
//   **全部为开** ⇒ 一律实现开启态,且不留 `#if`(`02` §1.1 规则 4)。
//   `_SUIT_ADDPART2` **为关** ⇒ 对应分支不实现。

#include "rules/Status.h"

#include "rules/Battle.h" // ★ 只为 checkCanAct —— DR-BT5 的唯一真源,不得在此重写一遍

namespace SA::Rules
{
namespace
{

using SA::Domain::BattleStatus;

// 守方对某状态的抵抗值(原 `RegTbl[status]` → `CHAR_getWorkInt`)。
//
// ⚠️★★ 照抄原版的**越界即取 0**(`battle_event.c:5119`):
//        if (status >= arraysizeof(RegTbl) || status < 0) Df_Reg = 0;
//    源码的 `arraysizeof(RegTbl)` 实测是 **31**(见 Status.h 的 kOriginalResistTableLen)
//    ⇒ 状态 31..43 抵抗恒 0。本批只到 11,两个上限都在这里显式表达 ——
//    ★ 别把 `status_resist` 的数组长度当成语义上限:**语义上限是 31**,
//      数组长度只是本批实现到哪。
std::int32_t resistOf(const Combatant &defender, int status) noexcept
{
	if (status < 0 || status >= kOriginalResistTableLen)
		return 0; // 原版越界防护 ⇒ 31..43 无法被抵抗(用户裁定照抄)
	// ★ P4: status_resist 已扩到 31,直接取。31..43 由上一行返回 0。
	return defender.mods.status_resist[status];
}

// F02: SSRC80 battle_event.c:5142–5151 比较 WORK 枚举 51/53/54，
// 而合法 status < 44。这些分支原本不可达，不能按同名状态重新激活。

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  状态命中判定 —— BATTLE_StatusAttackCheck(battle_event.c:5063)
// ═══════════════════════════════════════════════════════════════════
bool rollStatusAttack(bool is_pvp,
                      const Combatant &attacker,
                      const Combatant &defender,
                      int status,
                      int per_offset,
                      int range,
                      double bai,
                      Random &rng,
                      int *out_per) noexcept
{
	if (out_per != nullptr)
		*out_per = 0;

	// ── 前置 ①:状态号合法(`:5075`)────────────────────────────────
	//
	// ⚠️ 原版判的是 `status >= BATTLE_ST_END`(= 44)。IDL 的 `BattleStatus` 枚举
	//    **没有 END 这个取值**(`battle_status.proto:89` 只在注释里写了 44)
	//    ⇒ 用 Status.h 的 `kBattleStatusEnd` 常量,别去枚举里找它。
	if (status >= kBattleStatusEnd || status <= 0)
		return false;

	// ── 前置 ②:★★★ 全局互斥(`:5076-5079`)──────────────────────
	//
	// 原版扫全部 43 项 `StatusTbl`,任一 > 0 即失败。我们的模型是**单槽**
	// (`Combatant::status`)⇒ 等价条件是「槽非空」。
	// ⚠️★ 这个等价性成立**只因为**单槽建模本身就是全局互斥的产物 ——
	//    若将来有人把 status 改成位图,这一行会**静默地**只挡住一种状态。
	//    ⇒ Combatant.h 的注释与本行是同一条约束的两半,改一处必须改两处。
	// ⚠️★★ 它在**任何 RAND 之前** ⇒ 互斥不过时**不摇 rng**(同捕获的道具门 ④)。
	if (defender.status != static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE))
		return false;

	int per = 0;

	if (status == static_cast<int>(BattleStatus::BATTLE_ST_PARALYSIS))
	{
		// ── 麻痹走独立分支(`:5080-5084`)────────────────────────────
		//
		// ★ **固定基数 20**:不看等级差、不看幸运、不看体力、**没有装备抗性**、
		//   **没有 `max(per, 0)`**、**不夹 80 上限**。
		//   ⚠️ `05` §4.3 写了「− 装备抗性」与「max(per,0)」,源码都没有(纪律 ①)。
		//     缺 clamp 无可观察后果(`RAND(1,100) >= 1`)⇒ 照源码不补。
		per = kParalysisBasePer;
		per -= resistOf(defender, status);
	}
	else
	{
		// ── 通用分支(其余 42 种,`:5085-5155`)──────────────────────

		// ★ 体力占比。源码全程 float,`fVitalP` 先被当"四维和"用、再被覆盖成占比
		//   —— 这里拆成两个名字,数值路径不变。
		const float sum = static_cast<float>(defender.vital + defender.str +
		                                     defender.tough + defender.dex);
		// ⚠️ 原版没有除零保护:四维全 0 时 `templP` 是 inf/nan。
		//    我们的四维来源(rollSpawnStats / deriveBaseStats)恒 > 0,
		//    但 L3 不能假设调用方 —— ⇒ 显式挡 0 并**记明这是与原版的已知差**:
		//    原版在此会产出 nan 并让 `per` 变成未定义值(UB 面),不可复刻。
		float vital_p = 0.0F;
		if (sum > 0.0F)
			vital_p = static_cast<float>(defender.vital) / sum;

		// ★ 毒煞**反过来**取(`_PET_SKILL_SARS`,`:5096`):体力越高越容易中。
		if (status == static_cast<int>(BattleStatus::BATTLE_ST_SARS))
			vital_p = (1.0F - vital_p) * 0.9F;

		// ★ `/ 0.25` 然后 `× 10.0`。
		// ⚠️ 源码在 `× 10.0` 处按 `CHAR_WHICHTYPE` 分了**四条分支,而四条的值完全相同**
		//    (`:5102-5116`:玩家 / 宠物 / 敌人 / 其余 一律 `*= 10.0`)
		//    ⇒ 退化成一条(`05` §4.3 已注明「新实现只需一条」)。
		//    ★ 这是**冗余分支**:照抄成四个 if 不会更忠实,只会让人以为类型要紧。
		vital_p = vital_p / 0.25F * 10.0F;

		// ★ 等级差 × Bai,夹到 ±Range;PvP 时恒 0(`:5127-5134`)。
		int level = 0;
		if (!is_pvp)
		{
			level = attacker.level - defender.level;
			level = static_cast<int>(static_cast<double>(level) * bai);
		}
		if (level > range)
			level = range;
		if (level < -range)
			level = -range;

		// ★ 主式(`_SUIT_ADDENDUM` 开 ⇒ 减通用抗性,`:5136`)。
		// ⚠️★ `vital_p` 是 float 而 `per` 是 int ⇒ **这一行有隐式截断**,
		//    是逐位一致的关键点之一 ⇒ 保留源码的运算顺序与类型,别先聚合再转换。
		per = static_cast<int>(static_cast<float>(per_offset + level + attacker.luck -
		                                          resistOf(defender, status)) -
		                       vital_p -
		                       static_cast<float>(defender.mods.general_resist));

		// ★ 命中率硬上限 80%(`:5153`)—— 在 else 内 ⇒ 麻痹不吃(见 Status.h)。
		if (per > kStatusHitCap)
			per = kStatusHitCap;
	}

	if (out_per != nullptr)
		*out_per = per;

	// ★ 判定:`RAND(1, 100) < per`(`:5159`)—— **严格小于**(同暴击/逃跑/捕获)。
	return rng.rand(1, 100) < per;
}

// ═══════════════════════════════════════════════════════════════════
//  毒 / 剧毒的每回合掉血 —— Compute_Down(battle.c:5242)
// ═══════════════════════════════════════════════════════════════════
std::int32_t computePoisonDown(std::int32_t vital,
                               std::int32_t str,
                               std::int32_t dex,
                               std::int32_t tough,
                               std::int32_t hp) noexcept
{
	// ★ 全程整数除法(源码 `downs` 是 int)——`(((V+S+D+T)/100) - 20) / 4`。
	// ⚠️★ **照抄两步除法的形状,但不声称"合并会算错"**:穷举 Σ∈[0,2e6] 实测,
	//    两式仅在结果为负的区间分叉 495 处,而那一段被下面的「下限 1」全部吃掉
	//    ⇒ **clamp 之后 0 处分叉 = 等价**。照抄是为了与源码逐字一致,不是行为必需。
	//    (等价性要验不能推 —— 本注释的初稿写的是"不可合并",是编造的理由。)
	std::int32_t downs = vital + str + dex + tough;
	downs = ((downs / 100) - 20) / 4;
	if (downs < 1)
		downs = 1; // ★ 下限 1(四维不足时也掉 1 血)

	// ★★ **留 1 HP,毒绝不致死**(`:5258`)。
	// ⚠️ hp == 1 时 `downs = 0` ⇒ 返回 0,而调用方**照旧产出事件**
	//    (源码 `downs >= 0` 成立)⇒ 0 不代表"没生效"。
	if (hp <= downs)
		downs = hp - 1;
	if (downs < 0)
		downs = 0; // hp == 0(已死)时源码 `downs >= 0` 不成立 ⇒ 什么都不写

	return downs;
}

// ═══════════════════════════════════════════════════════════════════
//  每回合状态推进 —— BATTLE_StatusSeq(battle.c:5423)
// ═══════════════════════════════════════════════════════════════════
StatusTickResult tickStatus(const Combatant &c,
                            std::int32_t hp,
                            std::int32_t pet_hp,
                            bool has_ride_pet) noexcept
{
	StatusTickResult r;
	r.status = c.status;
	r.turns = c.status_turns;

	const int status = static_cast<int>(c.status);

	// ── 第 1 步:不能行动 ⇒ 清指令(`:5443`)────────────────────────
	//
	// ⚠️★★ **判据取递减之前的状态** ⇒ 本回合到期解除的角色,这一回合仍不能行动。
	//    这一步在递减循环**之前**,顺序即语义,不可后移。
	if (checkCanAct(c) != SA::Domain::CannotActReason::CANNOT_ACT_NONE)
		r.command_cleared = true;

	// 槽空 ⇒ 递减循环里 `cnt <= 0 continue` ⇒ 什么都不做。
	if (status == static_cast<int>(BattleStatus::BATTLE_ST_NONE) || c.status_turns <= 0)
		return r;

	// ── 第 2 步:递减 + 冻结(`:5448-5457`)────────────────────────
	//
	// ★★ 虚弱 / 魔障把递减**加回去** ⇒ 净效果不减 ⇒ 永不自然解除(见 Status.h)。
	//    ⚠️ 照抄源码的"先减再加回"而不是"判断后不减" —— 两者在本批等价,
	//      但源码那个形状是「所有状态都减、虚弱/魔障再补回」,而全局互斥让
	//      "所有状态"退化成它自己。★ 若将来职技路径能叠加状态(那 11 种),
	//      这个形状会开始产生差别:被叠加的状态也会被冻结。⇒ 保留源码形状。
	std::int32_t cnt = c.status_turns - 1;
	const bool frozen = (status == static_cast<int>(BattleStatus::BATTLE_ST_WEAKEN)) ||
	                    (status == static_cast<int>(BattleStatus::BATTLE_ST_BARRIER));
	if (frozen)
		cnt = cnt + 1; // ★ 加回去(源码是两个独立的 if,虚弱与魔障互斥 ⇒ 只会命中一个)
	r.turns = cnt;

	// ── 第 3 步:归零 ⇒ 解除(`:5475-5501`)────────────────────────
	// ★★ P4:冰爆术 (15, 35-43) **不受此分支约束** (`battle.c:5462-5474`):
	//    `if (cnt <= 0 && StatusTbl[i] != CHAR_WORKICECRACK && ...)` ——
	//    冰爆术在 cnt <= 0 时不走解除,而是进 switch 引爆。
	//    ⇒ 这里先判"是否冰爆术",是则跳过解除、直进第 4 步。
	const bool is_icecrack =
	    status == static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK) ||
	    (status >= static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK2) &&
	     status <= static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK10));
	if (cnt <= 0 && !is_icecrack)
	{
		r.cleared = true;
		r.status = static_cast<std::uint8_t>(BattleStatus::BATTLE_ST_NONE);
		r.turns = 0;

		// ★ 酒醉解除:敏捷回写(`:5490`)。
		// ⚠️★★ 有 ridepet 分支 —— 骑宠在场时 `quick += 骑宠 quick`,**不是 ×2**
		//    (`05` §4.4 漏了这一半)。★ 具体数值由调用方按 `has_ride_pet` 算
		//    (L3 看不到骑宠的 quick;它在 field 的另一个槽里)⇒ 这里只置标志。
		if (status == static_cast<int>(BattleStatus::BATTLE_ST_DRUNK))
			r.drunk_quick_restore = true;

		// ⚠️ 源码此处 `continue` ⇒ **解除的当回合不再结算该状态的伤害**
		//    (毒在最后一回合不掉血)。顺序即语义。
		return r;
	}

	// ── 第 4 步:未归零 ⇒ 按状态结算(`:5503` 的 switch)────────────
	switch (status)
	{
	case static_cast<int>(BattleStatus::BATTLE_ST_POISON):
	{
		// ★ 毒:**人物与骑宠各算一份**(`Compute_Down`,`battle.c:5251` / `:5267`)——
		//   同一条公式,各自的四维、各自的 HP、各自的"留 1 HP"夹取。
		// ⚠️★ 漏掉骑宠那半边不会有任何报错(骑宠照常在场、照常分摊伤害,只是毒
		//    不掉它的血)⇒ 与 M.1 断线回收漏了宠物同族 ⇒ 两半都算,用例两半都断言。
		r.hp_down = computePoisonDown(c.vital, c.str, c.dex, c.tough, hp);
		if (has_ride_pet)
		{
			r.pet_hp_down = computePoisonDown(c.ride_vital, c.ride_str, c.ride_dex,
			                                  c.ride_tough, pet_hp);
		}
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_DEEPPOISON):
	{
		// ★★ 剧毒(`_MAGIC_DEEPPOISON`,`battle.c:5536-5556`):
		//    **HP <= 1 或剩余回合 <= 1 ⇒ 直接死亡**,不掉血、不留 1 HP。
		// ⚠️ 判据用的是**递减前**的 `CHAR_WORKDEEPPOISON`(源码 `:5546` 读的是
		//    work 值,而 `--cnt` 已经写回去了 ⇒ 实际读到的是**递减后**的值)。
		//    ⇒ 这里用 `cnt`(递减后),与源码一致。
		if (hp <= 1 || cnt <= 1)
			r.deep_poison_kill = true;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_F_ENCLOSE):
	{
		// 源码 battle.c:5722-5755: DAMAGE = 50 * cnt，再过 PROFESSION_MAGIC_GET_DAMAGE
		// 原版 battle.c:5737:
		//   dec_hp = PROFESSION_MAGIC_GET_DAMAGE(charaindex, charaindex, 1, 50*cnt, 0)
		// magic_type=1 (火)，攻守同体
		r.hp_down = computeProfessionMagicDamage(1, 50 * cnt, c, c);
		if (r.hp_down > hp)
			r.hp_down = hp;
		break;
	}
	// ── P4:状态 12..43 的每回合效果 (`battle.c:5503` switch) ──
	case static_cast<int>(BattleStatus::BATTLE_ST_I_ENCLOSE):
	{
		// 冰附体 (`battle.c:5758-5770`):每回合 `dex = old_dex * 0.9`,
		// 写回 CHAR_WORKFIXDEX。调用方据此更新 fix_dex。
		// ★ 原版用 `CHAR_getInt(charaindex, CHAR_DEX)` 取 old_dex,但写的是
		//    WORKFIXDEX ⇒ 这里用 c.fix_dex (与原版 `CHAR_WORKFIXDEX` 对应)。
		r.i_enclose_dex_down = true;
		r.i_enclose_new_dex = static_cast<std::int32_t>(c.fix_dex * 0.9);
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_ICEARROW):
	{
		// 冰箭 (`battle.c:6089-6097`):每回合减敏
		// `dex = old_dex * (100 - dec_dex) / 100`, dec_dex = CHAR_WORKMODICEARROW。
		r.icearrow_dex_down = true;
		r.icearrow_new_dex = c.fix_dex * (100 - c.mods.icearrow_rate) / 100;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_OBLIVION):
	{
		// 遗忘 (`battle.c:5683-5705`):`cnt <= 1` 时恢复宠物召唤。
		// ★ 原版发送 `W<pet_no>` 给主人 + `BATTLE_BadStatusString(bid, 0)` +
		//    `BM|...|` 广播,并清状态。调用方据此产出事件。
		// ⚠️ 判据是 `cnt <= 1` (递减后剩 1 回合),不是 `cnt <= 0` ——
		//    提前一回合触发,此时还未进上面的解除分支。
		if (cnt <= 1)
			r.oblivion_restore_pet = true;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_RESIST_F):
	case static_cast<int>(BattleStatus::BATTLE_ST_RESIST_I):
	case static_cast<int>(BattleStatus::BATTLE_ST_RESIST_T):
	{
		// 火/冰/雷抗 (`battle.c:5773-5820`):`cnt <= 1` 时恢复抗性。
		// ★ 原版 `CHAR_WORK_F_RESIST -= CHAR_WORKMODRESIST_F`(等),并喊话。
		//    调用方据此把 element_resist 减去对应的 mod_resist_*。
		if (cnt <= 1)
			r.resist_restore = true;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_BLOODWORMS):
	{
		// 嗜血蛊 (`battle.c:6030-6115`):每回合吸血。
		//   dec_hp = skill_level * 10 + 30 (skill_level 经 PROFESSION_CHANGE_SKILL_LEVEL_M 转换)
		//   施蛊者回 dec_hp * rate / 100, rate 按等级: >=10→20, >=7→15, >=5→10, else→5。
		// ★ 骑宠在场时伤害减半,人宠各半 (`dec_hp /= 2`)。
		// ★ 调用方负责:扣被吸者 HP、加施蛊者 HP(上限 WORKMAXHP)。
		const std::int32_t skill_level = c.mods.bloodworms_level;
		std::int32_t dec_hp = skill_level * 10 + 30;
		if (has_ride_pet)
			dec_hp /= 2;
		std::int32_t rate = 5;
		if (skill_level >= 10)
			rate = 20;
		else if (skill_level >= 7)
			rate = 15;
		else if (skill_level >= 5)
			rate = 10;
		r.drain_hp = dec_hp;
		r.drain_caster = c.mods.bloodworms_caster;
		r.drain_heal_hp = dec_hp * rate / 100;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_SIGN):
	{
		// 一针见血 (`battle.c:5878-6030`):每回合吸 HP/MP。
		// ★ 8.0 有 `_PROFESSION_ADDSKILL` 开/关两套数值,本实现用开态
		//    (与 Status.h 卷首"宏门控的处置:全部为开"一致):
		//    Lv>=10: 200HP/30MP; Lv>6: 150/20; Lv>3: 100/15; else: 50/10。
		//    回复率: HP: Lv>8→100%, Lv>7→50%, else 0%; MP: Lv>8→100%, else 0%。
		// ★ 骑宠在场时伤害减半 (`dec_hp /= 2`)。
		// ★ 仅玩家扣 MP (`CHAR_WHICHTYPE == CHAR_TYPEPLAYER`)。
		const std::int32_t skill_level = c.mods.sign_level;
		std::int32_t dec_hp, dec_mp;
		if (skill_level >= 10)
		{
			dec_hp = 200;
			dec_mp = 30;
		}
		else if (skill_level > 6)
		{
			dec_hp = 150;
			dec_mp = 20;
		}
		else if (skill_level > 3)
		{
			dec_hp = 100;
			dec_mp = 15;
		}
		else
		{
			dec_hp = 50;
			dec_mp = 10;
		}
		if (has_ride_pet)
			dec_hp /= 2;
		std::int32_t hp_rate = 0, mp_rate = 0;
		if (skill_level > 8)
		{
			hp_rate = 100;
			mp_rate = 100;
		}
		else if (skill_level > 7)
		{
			hp_rate = 50;
		}
		r.drain_hp = dec_hp;
		// ★ MP 只在玩家身上扣,调用方按 kind 判断。
		r.drain_mp = (c.kind == SA::Rules::CombatantKind::kPlayer) ? dec_mp : 0;
		r.drain_caster = c.mods.sign_caster;
		r.drain_heal_hp = dec_hp * hp_rate / 100;
		r.drain_heal_mp = dec_mp * mp_rate / 100;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_INSTIGATE):
	{
		// 挑拨 (`battle.c:5831-5876`):80% 概率强制普攻己方随机目标,
		// 同时降攻防敏 `* (100 - rate) / 100` (rate = CHAR_WORKMODINSTIGATE)。
		// ★ 目标选择:己方 side 内随机 (`side = CHAR_WORKBATTLESIDE`)。
		// ★ 调用方负责:摇 80% (用 rng)、选随机目标、降三维。
		// ⚠️ 这里只置标志,rng 由调用方摇 (tickStatus 是 noexcept 纯函数,不摇 rng)。
		//    调用方用 `r.force_attack_random && rng.rand(1,100) <= 80` 判定。
		r.force_attack_random = true;
		r.force_attack_ally_side = true;
		r.instigate_stat_down = c.mods.instigate_rate;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_WORKANNEX):
	{
		// 附身 (`battle.c:5653-5681`):100% 强制普攻随机目标(任意 side)。
		// ★ 与混乱的区别:混乱 80% 概率 (`RAND(1,100) > 80` 则 break),
		//    附身无概率检查,必中。
		r.force_attack_random = true;
		r.force_attack_ally_side = false;
		break;
	}
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK2):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK3):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK4):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK5):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK6):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK7):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK8):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK9):
	case static_cast<int>(BattleStatus::BATTLE_ST_ICECRACK10):
	{
		// 冰爆术 1-10 (`battle.c:6121-6200`, 2-10 经 `WorkIceCrackPlay`)。
		// ★ 引爆条件:`cnt <= 0 && value > 0` (value = CHAR_WORKMODICECRACK)。
		// ★ 引爆伤害 = value,对敌方全体 (BATTLE_MultiList TARGET_SIDE_1)。
		// ★ 骑宠分摊: `playerdamage = (damage*petquick)/(defquick+petquick) + 1`,
		//    `petdamage = damage - playerdamage + 1`。
		// ⚠️ 调用方负责 AoE 结算,这里只标记"引爆"与"伤害值"。
		if (cnt <= 0 && c.mods.icecrack_value > 0)
		{
			r.icecrack_detonate = true;
			r.icecrack_damage = c.mods.icecrack_value;
		}
		break;
	}
	default:
		// 其余状态本回合无结算后果(麻痹/睡眠/石化只是"不能行动",
		// 由 checkCanAct 在派发时否决;酒醉/混乱的后果落在回避与目标选择里)。
		// ★ P4:晕眩(12)/树根缠绕(13)/天罗(14)/雷附体(23)无每回合效果;
		//    火附/冰附/雷附(28/29/30)/水附体(32)是攻击附魔标记,无每回合效果;
		//    专注(24)/三属抗(31)在 8.0 源码中被注释掉,不实现;
		//    恐惧(34)的 `if (cnt <= 0)` 在 8.0 是死代码(到不了 switch),不实现。
		break;
	}

	return r;
}

bool clearsCommandOnApply(int status) noexcept
{
	// 源码 `battle_event.c:2932-2937` 只列这四种。
	// ⚠️ 比 checkCanAct 的 8 项窄,不矛盾(见 Status.h)。
	return status == static_cast<int>(BattleStatus::BATTLE_ST_PARALYSIS) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_SLEEP) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_STONE) ||
	       status == static_cast<int>(BattleStatus::BATTLE_ST_BARRIER);
}

} // namespace SA::Rules
