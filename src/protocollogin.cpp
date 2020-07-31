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

#include "outputmessage.h"
#include "tasks.h"

#include "configmanager.h"
#include "iologindata.h"
#include "ban.h"
#include "game.h"

void ProtocolLogin::disconnectClient(const std::string& message)
{
  CanaryLib::NetworkMessage msg;
	msg.writeByte(0x0B);
	msg.writeString(message);

  send(msg.writeToFlatbuffersWrapper(OutputMessagePool::getOutputMessage()));

	disconnect();
}

#if GAME_FEATURE_SESSIONKEY > 0
void ProtocolLogin::getCharacterList(const std::string& accountName, const std::string& password, const std::string& token)
#else
void ProtocolLogin::getCharacterList(const std::string& accountName, const std::string& password)
#endif
{
	#if !(GAME_FEATURE_LOGIN_EXTENDED > 0)
	static uint32_t serverIp = INADDR_NONE;
	if (serverIp == INADDR_NONE) {
		std::string cfgIp = g_config().getString(ConfigManager::IP);
		serverIp = inet_addr(cfgIp.c_str());
		if (serverIp == INADDR_NONE) {
			struct hostent* he = gethostbyname(cfgIp.c_str());
			if (!he || he->h_addrtype != AF_INET) { //Only ipv4
				disconnectClient("ERROR: Cannot resolve hostname.");
				return;
			}
			memcpy(&serverIp, he->h_addr, sizeof(serverIp));
		}
	}
	#endif

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

  CanaryLib::NetworkMessage msg;

	#if GAME_FEATURE_SESSIONKEY > 0
	uint32_t ticks = time(nullptr) / AUTHENTICATOR_PERIOD;
	if (!account.key.empty()) {
		if (token.empty() || !(token == generateToken(account.key, ticks) || token == generateToken(account.key, ticks - 1) || token == generateToken(account.key, ticks + 1))) {
			msg.writeByte(0x0D);
			msg.writeByte(0);

      send(msg.writeToFlatbuffersWrapper(OutputMessagePool::getOutputMessage()));
			disconnect();
			return;
		}
		msg.writeByte(0x0C);
		msg.writeByte(0);
	}
	#endif

	//Update premium days
	Game::updatePremium(account);

	//Check for MOTD
	const std::string& motd = g_config().getString(ConfigManager::MOTD);
	if (!motd.empty()) {
		//Add MOTD
		msg.writeByte(CanaryLib::LoginServerMotd);

		std::ostringstream ss;
		ss << g_game().getMotdNum() << "\n" << motd;
		msg.writeString(ss.str());
	}

	#if GAME_FEATURE_SESSIONKEY > 0
	//Add session key
	msg.writeByte(0x28);
	msg.writeString(accountName + "\n" + password + "\n" + token + "\n" + std::to_string(ticks));
	#endif

	//Add char list
	msg.writeByte(CanaryLib::LoginServerCharacterList);

	#if GAME_FEATURE_LOGIN_EXTENDED > 0
	msg.writeByte(1); // number of worlds

	msg.writeByte(0); // world id
	msg.writeString(g_config().getString(ConfigManager::SERVER_NAME));
	msg.writeString(g_config().getString(ConfigManager::IP));
	msg.write<uint16_t>(g_config().getNumber(ConfigManager::GAME_PORT));
	msg.writeByte(0);

	uint8_t size = std::min<size_t>(std::numeric_limits<uint8_t>::max(), account.characters.size());
	msg.writeByte(size);
	for (uint8_t i = 0; i < size; i++) {
		msg.writeByte(0);
		msg.writeString(account.characters[i]);
	}
	#else
	uint8_t size = std::min<size_t>(std::numeric_limits<uint8_t>::max(), account.characters.size());
	msg.writeByte(size);
	for (uint8_t i = 0; i < size; i++) {
		msg.writeString(account.characters[i]);
		msg.writeString(g_config().getString(ConfigManager::SERVER_NAME));
		msg.write<uint32_t>(serverIp);
		msg.write<uint16_t>(g_config().getNumber(ConfigManager::GAME_PORT));
		#if GAME_FEATURE_PREVIEW_STATE > 0
		msg.writeByte(0);
		#endif
	}
	#endif

	//Add premium days
	#if GAME_FEATURE_LOGIN_PREMIUM_TIMESTAMP > 0
	#if GAME_FEATURE_LOGIN_PREMIUM_TYPE > 0
	msg.writeByte(0);
	#endif
	if (g_config().getBoolean(ConfigManager::FREE_PREMIUM)) {
		msg.writeByte(1);
		msg.write<uint32_t>(0);
	} else {
		msg.writeByte(account.premiumDays > 0 ? 1 : 0);
		msg.write<uint32_t>(time(nullptr) + (account.premiumDays * 86400));
	}
	#else
	if (g_config().getBoolean(ConfigManager::FREE_PREMIUM)) {
		msg.write<uint16_t>(0xFFFF); //client displays free premium
	} else {
		msg.write<uint16_t>(account.premiumDays);
	}
	#endif

  send(msg.writeToFlatbuffersWrapper(OutputMessagePool::getOutputMessage()));

	disconnect();
}

void ProtocolLogin::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game().getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	if (!decryptRSA(msg)) {
		disconnect();
		return;
	}

	uint32_t key[4] = {msg.read<uint32_t>(), msg.read<uint32_t>(), msg.read<uint32_t>(), msg.read<uint32_t>()};
	enableXTEAEncryption();
	setupXTEA(key);

	if (g_game().getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game().getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance.\nPlease re-connect in a while.");
		return;
	}

	#if GAME_FEATURE_ACCOUNT_NAME > 0
	std::string accountName = msg.readString();
	#else
	std::string accountName = std::to_string(msg.read<uint32_t>());
	#endif
	if (accountName.empty()) {
		disconnectClient("Invalid account name.");
		return;
	}

	std::string password = msg.readString();
	if (password.empty()) {
		disconnectClient("Invalid password.");
		return;
	}

	#if GAME_FEATURE_SESSIONKEY > 0
	// read authenticator token and stay logged in flag from last 128 bytes
	msg.skip((msg.getLength() - 128) - msg.getBufferPosition());
	if (!decryptRSA(msg)) {
		disconnectClient("Invalid authentification token.");
		return;
	}

	std::string authToken = msg.readString();

	auto thisPtr = std::static_pointer_cast<ProtocolLogin>(shared_from_this());
	g_dispatcher().addTask(std::bind(&ProtocolLogin::getCharacterList, thisPtr, std::move(accountName), std::move(password), std::move(authToken)));
	#else
	auto thisPtr = std::static_pointer_cast<ProtocolLogin>(shared_from_this());
	g_dispatcher().addTask(std::bind(&ProtocolLogin::getCharacterList, thisPtr, std::move(accountName), std::move(password)));
	#endif
}
