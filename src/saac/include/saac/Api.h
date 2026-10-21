// src/saac/include/saac/Api.h —— S22 saac 客户端与中心账号服务协议管线 (Phase 7)
//
// 依据: 00 §3.1 / §4.3 / §7 (D8 核心子系统 S22)
//       17-saac-boundary.md (W12 取证: 续体三元组/C33 世代隔离/C35 显式请求关联/C45 锁模型)
// 约束: 本头文件为 src/saac 模块对外暴露的唯一样板头 (PUBLIC_HEADER = "Api.h")。

#ifndef __SA_Saac_Api_H__
#define __SA_Saac_Api_H__

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "domain/character_data.sa.h"
#include "transport/interservice.sa.h"
#include "transport/login.sa.h"

namespace SA::Saac
{

// ── 账号鉴权与通信状态诊断 ──────────────────────────────────────────
enum class SaacAccountStatus : std::uint32_t
{
	kOk = 0,
	kInvalidCredentials = 1,
	kAccountLocked = 2,
	kAccountBusy = 3,
	kAccountNotFound = 4,
	kTimeout = 5,
	kNetworkError = 6,
	kStaleGeneration = 7, // 跨实例错发防护拦截 (原版 fdid 归零缺陷消弭 C33)
	kDuplicateLock = 8,
	kLeaseExpired = 9,
};

// ── 账号排他锁与租约类型 (消弭原版 lock.c 移民锁误解缺陷 C45) ────────
enum class AccountLockType : std::uint8_t
{
	kUnlock = 0,        // 释放锁
	kLoginLock = 1,     // 登入独占锁
	kMigrationLock = 2, // 星系移民/跨服迁移锁
};

// ── 协议帧报文类型 ───────────────────────────────────────────────────
enum class PacketType : std::uint16_t
{
	kNone = 0,
	kLoginReq = 0x0101,
	kLoginResp = 0x0102,
	kCharLoadReq = 0x0201,
	kCharLoadResp = 0x0202,
	kCharSaveReq = 0x0203,
	kCharSaveResp = 0x0204,
	kCharDeleteReq = 0x0205,  // ★ #43：原 saacproto_ACCharDelete_recv (saacproto_serv.h:56)
	kCharDeleteResp = 0x0206, // ★ #43：原 saacproto_ACCharDelete_send (saacproto_serv.h:57)
	kLockReq = 0x0301,
	kLockResp = 0x0302,
	kUnlockReq = 0x0303,
	kUnlockResp = 0x0304,
	kBroadcastReq = 0x0401,
	kBroadcastAck = 0x0402,
};

// ── 请求与响应信封 (00 §3.1 强规范) ─────────────────────────────────
struct RequestEnvelope
{
	std::uint32_t instance_id = 1;   // 线路实例 ID
	std::uint32_t generation = 1;    // 启动世代号
	std::uint64_t request_id = 0;    // 进程内唯一单调递增 ID
	std::int64_t deadline_ms = 5000; // 超时时间戳/窗口
};

struct ResponseEnvelope
{
	std::uint32_t instance_id = 1;
	std::uint32_t generation = 1;
	std::uint64_t request_id = 0;
	SaacAccountStatus status = SaacAccountStatus::kOk;
	std::uint32_t error_code = 0;
};

// ── 回调函数类型 ───────────────────────────────────────────────────
using LoginCallback = std::function<void(SaacAccountStatus status,
                                         const std::string &cdkey,
                                         const std::vector<SA::Transport::CharacterSummary> &chars)>;

using CharLoadCallback = std::function<void(SaacAccountStatus status,
                                            const SA::Domain::CharacterRecord &record)>;

using CharSaveCallback = std::function<void(SaacAccountStatus status,
                                            std::uint64_t revision)>;

using LockCallback = std::function<void(SaacAccountStatus status,
                                        const std::string &lease_token)>;

using CharDeleteCallback = std::function<void(SaacAccountStatus status,
                                              const std::string &result)>;

using BroadcastCallback = std::function<void(bool success)>;

// ── 跨线路广播事件消息 (8.0 独有进第一批, 对齐 00 §7) ───────────────
struct WorldBroadcastMessage
{
	int channel = 0;
	std::string sender;
	std::string text;
	std::int64_t timestamp_ms = 0;
};

using BroadcastListener = std::function<void(const WorldBroadcastMessage &msg)>;
using BroadcastSender = std::function<void(const WorldBroadcastMessage &msg)>;

// ── S22 saac 客户端抽象接口 ─────────────────────────────────────────
class ISaacClient
{
  public:
	virtual ~ISaacClient() = default;

	// 线路元数据与世代管理
	virtual std::uint32_t instanceId() const noexcept = 0;
	virtual std::uint32_t generation() const noexcept = 0;
	virtual void setGeneration(std::uint32_t gen) noexcept = 0;

	// 核心业务 RPC (带续体三元组自动装配与超时跟踪)
	virtual std::uint64_t requestLogin(const std::string &cdkey,
	                                   const std::string &password,
	                                   LoginCallback cb,
	                                   std::int64_t timeout_ms = 5000) = 0;

	virtual std::uint64_t requestCharLoad(const std::string &cdkey,
	                                      int slot,
	                                      CharLoadCallback cb,
	                                      std::int64_t timeout_ms = 5000) = 0;
	// ★ #45：原版 ACCharLoad（recv.c:102-106）内部先 sasql_query 验密，
	//   失败则 sasql_register 自动注册新账号（WON 时代行为），无独立登录协议。
	//   实现侧 requestLogin 为独立 stub；服务端实现账号逻辑时需决策：
	//   保留自动注册（对齐原版）或改走显式注册流程。

	virtual std::uint64_t requestCharSave(const std::string &cdkey,
	                                      int slot,
	                                      const SA::Domain::CharacterRecord &record,
	                                      bool logout,
	                                      const std::string &lease_token,
	                                      CharSaveCallback cb,
	                                      std::int64_t timeout_ms = 5000) = 0;

	virtual std::uint64_t requestLock(const std::string &cdkey,
	                                  AccountLockType lock_type,
	                                  LockCallback cb,
	                                  std::int64_t timeout_ms = 5000) = 0;

	virtual std::uint64_t requestUnlock(const std::string &cdkey,
	                                    const std::string &lease_token,
	                                    LockCallback cb,
	                                    std::int64_t timeout_ms = 5000) = 0;

	// ★ #43：角色删除（原 saacproto_ACCharDelete_recv, saacproto_serv.h:56）
	virtual std::uint64_t requestCharDelete(const std::string &cdkey,
	                                        const std::string &password,
	                                        const std::string &charname,
	                                        const std::string &option,
	                                        CharDeleteCallback cb,
	                                        std::int64_t timeout_ms = 5000) = 0;

	// ★ #44：锁状态查询（原 saacproto_ACLock_recv lock=2, recv.c:341-348）
	//   返回 "USRLOCKED:..." 或 "USRUNLOCKED:..."
	virtual std::uint64_t requestLockQuery(const std::string &cdkey,
	                                       LockCallback cb,
	                                       std::int64_t timeout_ms = 5000) = 0;

	// ★ #44：GM 强制解锁单个账号（原 saacproto_ACLock_recv lock=3, recv.c:349-356）
	//   返回 "GMUNLOCKSUCCESS" 或 "GMUNLOCKFAIL"
	virtual std::uint64_t requestGMUnlock(const std::string &cdkey,
	                                      LockCallback cb,
	                                      std::int64_t timeout_ms = 5000) = 0;

	// ★ #44：GM 解锁整个服务器（原 saacproto_ACLock_recv lock=4, recv.c:357-360）
	//   返回 "GMUNLOCKALL"
	virtual std::uint64_t requestGMUnlockAll(const std::string &server_id,
	                                         LockCallback cb,
	                                         std::int64_t timeout_ms = 5000) = 0;

	// 跨线路全服广播/聊天室 (8.0 独有进第一批)
	virtual std::uint64_t broadcastWorldMessage(int channel,
	                                            const std::string &sender,
	                                            const std::string &text,
	                                            BroadcastCallback cb = nullptr) = 0;

	virtual void registerBroadcastListener(BroadcastListener listener) = 0;
	virtual void setBroadcastSender(BroadcastSender sender) = 0;
	virtual void feedBroadcastMessage(const WorldBroadcastMessage &msg) = 0;

	// 驱动推进与超时扫描
	virtual void tick(std::int64_t now_ms) = 0;

	// 诊断观察面
	virtual std::size_t inFlightCount() const noexcept = 0;
	virtual std::uint64_t staleDropCount() const noexcept = 0;
	virtual std::uint64_t timeoutDropCount() const noexcept = 0;

	// 模拟或外部回包注入 (用于确定性测试与分片通信接线)
	virtual bool feedResponse(const ResponseEnvelope &header,
	                          std::function<void()> payload_dispatcher = nullptr) = 0;

	virtual bool completeInFlight(std::uint64_t request_id, SaacAccountStatus status,
	                              const std::vector<std::uint8_t> &payload = {}) = 0;
};

// 工厂函数: 构造标准 SaacClient 实例
std::unique_ptr<ISaacClient> createSaacClient(std::uint32_t instance_id, std::uint32_t generation);

// 协议帧打包与解包
std::vector<std::uint8_t> encodeRequest(PacketType type, const RequestEnvelope &env,
                                        const std::vector<std::uint8_t> &payload = {});

bool decodeResponse(const std::uint8_t *data, std::size_t size,
                    PacketType &out_type, ResponseEnvelope &out_env,
                    std::vector<std::uint8_t> &out_payload);

} // namespace SA::Saac

#endif // __SA_Saac_Api_H__
