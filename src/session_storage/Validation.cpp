#include "data/Json.h"
#include "session_storage/Api.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace SA::SessionStorage
{
bool loadSettingsFile(const std::string &path, Settings &settings, std::string &error)
{
	try
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::invalid_argument("storage settings file unavailable");
		const std::string text((std::istreambuf_iterator<char>(input)), {});
		if (text.size() > 65536)
			throw std::invalid_argument("storage settings too large");
		const auto parsed = SA::Data::Json::parse(text);
		if (!parsed.ok || !parsed.value.isObject())
			throw std::invalid_argument("invalid storage settings JSON");
		const auto str = [&](const char *name)
		{
			const auto *v = parsed.value.find(name);
			if (!v || !v->isString() || v->asString().empty())
				throw std::invalid_argument(std::string("missing setting: ") + name);
			return v->asString();
		};
		const auto port = [&](const char *name)
		{
			const auto *v = parsed.value.find(name);
			if (!v || !v->isNumber() || v->asNumber() < 1 || v->asNumber() > 65535 || std::trunc(v->asNumber()) != v->asNumber())
				throw std::invalid_argument(std::string("invalid port: ") + name);
			return static_cast<int>(v->asNumber());
		};
		settings.mysql_host = str("mysql_host");
		settings.mysql_port = port("mysql_port");
		settings.mysql_user = str("mysql_user");
		settings.mysql_password = str("mysql_password");
		settings.mysql_ca = str("mysql_ca");
		settings.redis_host = str("redis_host");
		settings.redis_port = port("redis_port");
		settings.redis_password = str("redis_password");
		return true;
	}
	catch (const std::exception &exception)
	{
		error = exception.what();
		return false;
	}
}

bool validRecord(const SA::Domain::CharacterRecord &record)
{
	const auto &p = record.player;
	if (record.schema_ver != 1 || p.name.empty() || p.name.size() > 31 || p.dir > 7 ||
	    p.level < 1 || p.level > 200 || p.exp < 0 || p.hp < 0 || p.hp > 100000000 ||
	    p.mp < 0 || p.mp > p.max_mp || p.max_mp > 1000000 ||
	    p.default_pet < -1 || p.default_pet > 4 || p.capture_count < 0 ||
	    p.vital < 0 || p.str < 0 || p.tough < 0 || p.dex < 0 ||
	    p.vital > 10000000 || p.str > 10000000 || p.tough > 10000000 || p.dex > 10000000 ||
	    record.pets.size() > 5 || record.items.size() > 54)
		return false;
	std::set<std::uint32_t> slots;
	std::set<std::uint64_t> ids;
	for (const auto &pet : record.pets)
	{
		if (pet.slot >= 5 || !slots.insert(pet.slot).second ||
		    (pet.uid && !ids.insert(pet.uid).second) || pet.value.level < 1 || pet.value.hp < 0 ||
		    pet.value.growth_vital < 0 || pet.value.growth_vital > 255 ||
		    pet.value.growth_str < 0 || pet.value.growth_str > 255 ||
		    pet.value.growth_tough < 0 || pet.value.growth_tough > 255 ||
		    pet.value.growth_dex < 0 || pet.value.growth_dex > 255)
			return false;
	}
	if (p.default_pet >= 0 && !slots.count(static_cast<std::uint32_t>(p.default_pet)))
		return false;
	slots.clear();
	ids.clear();
	for (const auto &item : record.items)
		if (item.slot >= 54 || !slots.insert(item.slot).second ||
		    (item.uid && !ids.insert(item.uid).second) || item.value.item_id <= 0 || item.value.current_pile < 0)
			return false;
	return true;
}

class MemoryStorage final : public Service
{
	struct AccountData
	{
		std::string password;
		std::vector<std::uint64_t> char_ids;
	};
	std::unordered_map<std::string, AccountData> accounts_;
	std::unordered_map<std::uint64_t, SA::Domain::CharacterRecord> characters_;
	std::uint64_t next_char_id_ = 1000;
	std::vector<Completion> completions_;

  public:
	MemoryStorage()
	{
		AccountData test_acc;
		test_acc.password = "stoneage123456";
		const std::uint64_t cid = 1001;
		test_acc.char_ids.push_back(cid);
		accounts_["tester"] = test_acc;

		SA::Domain::CharacterRecord test_char{};
		test_char.schema_ver = 1;
		test_char.char_id = cid;
		test_char.revision = 1;
		(void)test_char.player.name.assign("石器探索者");
		test_char.player.level = 1;
		test_char.player.hp = 100;
		test_char.player.mp = test_char.player.max_mp = 100;
		test_char.player.vital = 100;
		test_char.player.str = 100;
		test_char.player.tough = 100;
		test_char.player.dex = 100;
		test_char.player.earth = 5;
		test_char.player.water = 5;
		test_char.player.image = 100000;
		test_char.player.face_image = 30000;
		test_char.player.floor = 100;
		test_char.player.x = 643;
		test_char.player.y = 459;
		test_char.player.dir = 5;
		test_char.player.default_pet = 0;

		SA::Domain::PetSlot pet{};
		pet.slot = 0;
		pet.uid = 101;
		pet.value.base_image = 100054;
		pet.value.level = 1;
		pet.value.hp = 30;
		pet.value.vital = 10;
		pet.value.str = 8;
		pet.value.tough = 6;
		pet.value.dex = 5;
		pet.value.earth = 3;
		pet.value.fire = 7;
		(void)pet.value.name.assign("乌力");
		(void)test_char.pets.push_back(pet);

		characters_[cid] = test_char;
	}

	bool submit(Request req) override
	{
		Completion c{};
		c.operation = req.operation;
		c.session = req.session;
		c.correlation = req.correlation;
		c.logout = req.logout;
		c.code = SA::Transport::AccountCode::ACCOUNT_OK;

		if (req.operation == Operation::kLogin)
		{
			const std::string login_str = req.login.login.c_str();
			const std::string pwd_str = req.login.password.c_str();
			auto it = accounts_.find(login_str);
			if (it == accounts_.end())
			{
				AccountData new_acc;
				new_acc.password = pwd_str;
				accounts_[login_str] = new_acc;
				it = accounts_.find(login_str);
			}
			for (std::uint64_t cid : it->second.char_ids)
			{
				auto cit = characters_.find(cid);
				if (cit != characters_.end())
				{
					SA::Transport::CharacterSummary sum{};
					sum.char_id = cit->second.char_id;
					sum.level = cit->second.player.level;
					sum.image = cit->second.player.image;
					(void)sum.name.assign(cit->second.player.name.c_str());
					(void)c.characters.push_back(sum);
				}
			}
		}
		else if (req.operation == Operation::kCreate)
		{
			const std::uint64_t new_id = ++next_char_id_;
			req.character.char_id = new_id;
			req.character.schema_ver = 1;
			characters_[new_id] = req.character;
			c.character = req.character;
		}
		else if (req.operation == Operation::kSelect)
		{
			auto cit = characters_.find(req.character.char_id);
			if (cit != characters_.end())
			{
				c.character = cit->second;
			}
			else
			{
				c.code = SA::Transport::AccountCode::ACCOUNT_UNAVAILABLE;
			}
		}
		else if (req.operation == Operation::kSave)
		{
			characters_[req.character.char_id] = req.character;
			c.character = req.character;
		}
		completions_.push_back(std::move(c));
		return true;
	}

	std::vector<Completion> poll() override
	{
		auto out = std::move(completions_);
		completions_.clear();
		return out;
	}

	bool idle() const override
	{
		return completions_.empty();
	}
};

std::unique_ptr<Service> createMemoryStorage()
{
	return std::make_unique<MemoryStorage>();
}
} // namespace SA::SessionStorage
