/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2020  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "protocollogin.h"

#include "flatbuffers_wrapper_pool.h"
#include "tasks.h"

#include "configmanager.h"
#include "iologindata.h"
#include "ban.h"
#include "game.h"

void ProtocolLogin::disconnectClient(const std::string& message)
{
  Wrapper_ptr wrapper = FlatbuffersWrapperPool::getOutputWrapper();
  flatbuffers::FlatBufferBuilder &fbb = wrapper->Builder();

  auto error_message = fbb.CreateString(message);
  auto error = CanaryLib::CreateErrorData(fbb, error_message);
  wrapper->add(error.Union(), CanaryLib::DataType_ErrorData);
  
  send(wrapper);

	disconnect();
}

void ProtocolLogin::getCharacterList(const std::string& accountName, const std::string& password, const std::string& token)
{
	auto connection = getConnection();
	if (!connection) {
		return;
	}

	BanInfo banInfo;
	if (IOBan::isIpBanned(connection->getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		std::ostringstream ss;
		ss << "Your IP has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy << ".\n\nReason specified:\n" << banInfo.reason;
		disconnectClient(ss.str());
		return;
	}

	Account account;
	if (!IOLoginData::loginserverAuthentication(accountName, password, account)) {
		disconnectClient("Account name or password is not correct.");
		return;
	}

	uint32_t ticks = time(nullptr) / AUTHENTICATOR_PERIOD;
	if (!account.key.empty()) {
		if (token.empty() || !(token == generateToken(account.key, ticks) || token == generateToken(account.key, ticks - 1) || token == generateToken(account.key, ticks + 1))) {
			disconnectClient("Invalid authentification token.");
			return;
		}
	}

	//Update premium days
	Game::updatePremium(account);

  Wrapper_ptr wrapper = FlatbuffersWrapperPool::getOutputWrapper();
  flatbuffers::FlatBufferBuilder &fbb = wrapper->Builder();
  
  auto sessionKey = fbb.CreateString(accountName + "\n" + password + "\n" + token + "\n" + std::to_string(ticks));
  CanaryLib::AccountInfoBuilder accountBuilder(fbb);
  accountBuilder.add_session_key(sessionKey);
  accountBuilder.add_premium_days(account.premiumDays);
  accountBuilder.add_free_premium(g_config().getBoolean(ConfigManager::FREE_PREMIUM));
  auto accountInfo = accountBuilder.Finish();

  auto worldIp = fbb.CreateString(g_config().getString(ConfigManager::IP));
  auto worldName = fbb.CreateString(g_config().getString(ConfigManager::SERVER_NAME));
  CanaryLib::WorldInfoBuilder worldBuilder(fbb);
  worldBuilder.add_ip(worldIp);
  worldBuilder.add_name(worldName);
  worldBuilder.add_port(g_config().getNumber(ConfigManager::GAME_PORT));
  auto worldInfo = worldBuilder.Finish();

  std::vector<flatbuffers::Offset<CanaryLib::CharacterInfo>> character_vector;
	uint8_t size = std::min<size_t>(std::numeric_limits<uint8_t>::max(), account.characters.size());
	for (uint8_t i = 0; i < size; i++) {
    auto characterName = fbb.CreateString(account.characters[i]);
    auto characterInfo = CanaryLib::CreateCharacterInfo(fbb, characterName);
    character_vector.emplace_back(characterInfo);
	}
  auto charactersList = fbb.CreateVector(character_vector);

	const std::string& motd_str = g_config().getString(ConfigManager::MOTD);
  flatbuffers::Offset<flatbuffers::String> motd;
	if (!motd_str.empty()) {
    motd = fbb.CreateString(
      std::to_string(g_game().getMotdNum()) + "\n" + motd_str
    );
	}

  auto characters = CanaryLib::CreateCharactersListData(
    fbb,
    accountInfo,
    charactersList,
    worldInfo,
    motd
  );
  wrapper->add(characters.Union(), CanaryLib::DataType_CharactersListData);
  send(wrapper);

	disconnect();
}

void ProtocolLogin::parseLoginData(const CanaryLib::LoginInfo * login_info) {
  switch (g_game().getGameState()) {
    case GAME_STATE_SHUTDOWN:
      disconnect();
      return;
    case GAME_STATE_STARTUP:
      disconnectClient("Gameworld is starting up.\nPlease wait.");
      return;
    case GAME_STATE_MAINTAIN:
      disconnectClient("Gameworld is under maintenance..\nPlease re-connect in a while.");
      return;
    default:
      break;
  }

  if (!login_info->account()->size() || !login_info->password()->size()) {
		disconnectClient("Account name and password cannot be empty.");
		return;
  }

  setupXTEA(login_info->xtea_key()->data());
	g_dispatcher().addTask(
    std::bind(
      &ProtocolLogin::getCharacterList, 
      std::static_pointer_cast<ProtocolLogin>(shared_from_this()), 
      std::move(login_info->account()->str()), 
      std::move(login_info->password()->str()), 
      std::move("")
    )
  );
}
