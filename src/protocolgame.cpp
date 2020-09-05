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

#include "protocolgame.h"

#include "modules.h"
#include "flatbuffers_wrapper_pool.h"

#include "player.h"
#include "monsters.h"

#include "databasetasks.h"
#include "configmanager.h"
#include "actions.h"
#include "game.h"
#include "iologindata.h"
#include "iomarket.h"
#include "waitlist.h"
#include "ban.h"
#include "spells.h"

extern Actions actions;

void ProtocolGame::release()
{
	//dispatcher thread
	if (player && player->client == shared_from_this()) {
		player->client.reset();
		player->decrementReferenceCounter();
		player = nullptr;
	}

	FlatbuffersWrapperPool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

void ProtocolGame::login(const std::string& accountName, const std::string& password, std::string& characterName, std::string& token, uint32_t tokenTime, OperatingSystem_t operatingSystem, OperatingSystem_t tfcOperatingSystem)
{
	//dispatcher thread
	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		std::ostringstream ss;
		ss << "Your IP has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy << ".\n\nReason specified:\n" << banInfo.reason;
		disconnectClient(ss.str());
		return;
	}
	
	uint32_t accountId = IOLoginData::gameworldAuthentication(accountName, password, characterName, token, tokenTime);
	if (accountId == 0) {
		disconnectClient("Account name or password is not correct.");
		return;
	}

	Player* foundPlayer = g_game().getPlayerByName(characterName);
	if (!foundPlayer || g_config().getBoolean(ConfigManager::ALLOW_CLONES)) {
		player = new Player(getThis());
		player->setName(characterName);

		player->incrementReferenceCounter();

		if (!IOLoginData::preloadPlayer(player, characterName)) {
			disconnectClient("Your character could not be loaded.");
			return;
		}

		player->setID();
		if (IOBan::isPlayerNamelocked(player->getGUID())) {
			disconnectClient("Your character has been namelocked.");
			return;
		}

		if (g_game().getGameState() == GAME_STATE_CLOSING && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("The game is just going down.\nPlease try again later.");
			return;
		}

		if (g_game().getGameState() == GAME_STATE_CLOSED && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("Server is currently closed.\nPlease try again later.");
			return;
		}

		if (g_config().getBoolean(ConfigManager::ONE_PLAYER_ON_ACCOUNT) && player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER && g_game().getPlayerByAccount(player->getAccount())) {
			disconnectClient("You may only login with one character\nof your account at the same time.");
			return;
		}

		if (!player->hasFlag(PlayerFlag_CannotBeBanned)) {
			if (IOBan::isAccountBanned(accountId, banInfo)) {
				if (banInfo.reason.empty()) {
					banInfo.reason = "(none)";
				}

				std::ostringstream ss;
				if (banInfo.expiresAt > 0) {
					ss << "Your account has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy << ".\n\nReason specified:\n" << banInfo.reason;
				} else {
					ss << "Your account has been permanently banned by " << banInfo.bannedBy << ".\n\nReason specified:\n" << banInfo.reason;
				}
				disconnectClient(ss.str());
				return;
			}
		}

		std::size_t currentSlot;
		if (!WaitingList::getInstance().clientLogin(player, currentSlot)) {
			int64_t retryTime = WaitingList::getTime(currentSlot);
			std::ostringstream ss;

			ss << "Too many players online.\nYou are at place "
			   << currentSlot << " on the waiting list.";

			CanaryLib::NetworkMessage msg;
			msg.writeByte(CanaryLib::GameServerLoginWait);
			msg.writeString(ss.str());
			msg.writeByte(static_cast<uint8_t>(retryTime));

      Wrapper_ptr wrapper = FlatbuffersWrapperPool::getOutputWrapper();
      wrapper->addRawMessage(msg);
			send(wrapper);
			disconnect();
			return;
		}

		if (!IOLoginData::loadPlayerById(player, player->getGUID())) {
			disconnectClient("Your character could not be loaded.");
			return;
		}
		
		player->setOperatingSystem(operatingSystem);
		player->setTfcOperatingSystem(tfcOperatingSystem);
		if (!g_game().placeCreature(player, player->getLoginPosition())) {
			if (!g_game().placeCreature(player, player->getTemplePosition(), false, true)) {
				disconnectClient("Temple position is wrong. Contact the administrator.");
				return;
			}
		}

		if (operatingSystem >= CLIENTOS_OTCLIENT_LINUX) {
			NetworkMessage opcodeMessage;
			opcodeMessage.writeByte(CanaryLib::GameServerExtendedOpcode);
			opcodeMessage.writeByte(0x00);
			opcodeMessage.write<uint16_t>(0x00);
			writeToOutputBuffer(opcodeMessage);

			player->registerCreatureEvent("ExtendedOpcode");
		}

		player->lastIP = player->getIP();
		player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
		acceptPackets = true;
	} else {
		if (eventConnect != 0 || !g_config().getBoolean(ConfigManager::REPLACE_KICK_ON_LOGIN)) {
			//Already trying to connect
			disconnectClient("You are already logged in.");
			return;
		}

		if (foundPlayer->client) {
			foundPlayer->disconnect();
			foundPlayer->isConnecting = true;

			eventConnect = g_dispatcher().addEvent(1000, std::bind(&ProtocolGame::connect, getThis(), foundPlayer->getID(), operatingSystem, tfcOperatingSystem));
		} else {
			connect(foundPlayer->getID(), operatingSystem, tfcOperatingSystem);
		}
	}
	FlatbuffersWrapperPool::getInstance().addProtocolToAutosend(shared_from_this());
}

void ProtocolGame::connect(uint32_t playerId, OperatingSystem_t operatingSystem, OperatingSystem_t tfcOperatingSystem)
{
	eventConnect = 0;

	if (isConnectionExpired()) {
		//ProtocolGame::release() has been called at this point and the Connection object
		//no longer exists, so we return to prevent leakage of the Player.
		return;
	}

	Player* foundPlayer = g_game().getPlayerByID(playerId);
	if (!foundPlayer || foundPlayer->client) {
		disconnectClient("You are already logged in.");
		return;
	}

	player = foundPlayer;
	player->incrementReferenceCounter();

	g_chat().removeUserFromAllChannels(*player);
	player->clearModalWindows();
	player->setOperatingSystem(operatingSystem);
	player->setTfcOperatingSystem(tfcOperatingSystem);
	player->isConnecting = false;

	player->client = getThis();
	sendAddCreature(player, player->getPosition(), 0, false);
	g_chat().openChannelsByServer(player);
	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	acceptPackets = true;
}

void ProtocolGame::logout(bool displayEffect, bool forced)
{
	//dispatcher thread
	if (!player) {
		return;
	}

	if (!player->isRemoved()) {
		if (!forced) {
			if (!player->isAccessPlayer()) {
				if (player->getTile()->hasFlag(TILESTATE_NOLOGOUT)) {
					player->sendCancelMessage(RETURNVALUE_YOUCANNOTLOGOUTHERE);
					return;
				}

				if (!player->getTile()->hasFlag(TILESTATE_PROTECTIONZONE) && player->hasCondition(CONDITION_INFIGHT)) {
					player->sendCancelMessage(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
					return;
				}
			}

			//scripting event - onLogout
			if (!g_creatureEvents().playerLogout(player)) {
				//Let the script handle the error message
				return;
			}
		}

		if (displayEffect && player->getHealth() > 0) {
			g_game().addMagicEffect(player->getPosition(), CONST_ME_POFF);
		}
	}

	disconnect();

	g_game().removeCreature(player);
}

void ProtocolGame::parseLoginInfo(const CanaryLib::LoginInfo * login_info) 
{
  if (!login_info) {
    disconnectClient("Malformed login info");
    return; 
  }

  setupXTEA(login_info->xtea_key()->data());

  auto game_login_info = login_info->game_login_info();
  auto session_key = game_login_info->session_key()->str();
  auto session_args = explodeString(session_key, "\n", 4);
	if (session_args.size() != 4) {
		disconnectClient("Invalid session key.");
		return;
	}

  std::string characterName = game_login_info->char_name()->str();
  if (characterName.empty()) {
		disconnectClient("Invalid character name.");
		return;
  }

	std::string& accountName = session_args[0];
	std::string& password = session_args[1];
	std::string& token = session_args[2];

  if (accountName.empty() || password.empty()) {
		disconnectClient("You must enter your account name and password.");
		return;
  }

  // Validate token (session_args[3])
	uint32_t tokenTime = 0;
	try {
		tokenTime = std::stoul(session_args[3]);
	} catch (const std::invalid_argument&) {
		disconnectClient("Invalid session key.");
		return;
	} catch (const std::out_of_range&) {
		disconnectClient("Token time is too long.");
		return;
	}

	g_dispatcher().addTask(std::bind(
      &ProtocolGame::login,
      getThis(),
      std::move(accountName),
      std::move(password),
      std::move(characterName),
      std::move(token),
      tokenTime,
      OperatingSystem_t::CLIENTOS_OTCLIENT_LINUX,
      OperatingSystem_t::CLIENTOS_NONE
    ));
}

void ProtocolGame::onConnect()
{
	CanaryLib::NetworkMessage msg;
	static std::random_device rd;
	static std::ranlux24 generator(rd());
	static std::uniform_int_distribution<uint16_t> randNumber(0x00, 0xFF);

	msg.writeByte(CanaryLib::GameServerChallenge);

	// Add timestamp & random number
	challengeTimestamp = static_cast<uint32_t>(time(nullptr));
	msg.write<uint32_t>(challengeTimestamp);

	challengeRandom = randNumber(generator);
	msg.writeByte(challengeRandom);

  auto wrapper = FlatbuffersWrapperPool::getOutputWrapper();
  wrapper->disableEncryption();
  wrapper->addRawMessage(msg);
  send(wrapper);
}

void ProtocolGame::disconnectClient(const std::string& message) const
{
	CanaryLib::NetworkMessage msg;
	msg.writeByte(CanaryLib::GameServerLoginError);
	msg.writeString(message);

  Wrapper_ptr wrapper = FlatbuffersWrapperPool::getOutputWrapper();
  wrapper->addRawMessage(msg);
  send(wrapper);
	disconnect();
}

void ProtocolGame::writeToOutputBuffer()
{
  writeToOutputBuffer(playermsg);
}

void ProtocolGame::writeToOutputBuffer(NetworkMessage& msg)
{
  Wrapper_ptr wrapper = getOutputBuffer(msg.getLength());
  wrapper->addRawMessage(msg);
}

void ProtocolGame::parsePacket(NetworkMessage& msg)
{
  input_msg = msg;
	if (!acceptPackets || g_game().getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() <= 0) {
		return;
	}

	uint8_t recvbyte = input_msg.readByte();
	if (!player) {
		if (recvbyte == CanaryLib::ClientEnterGame) {
			disconnect();
		}
		return;
	}

	//a dead player can not performs actions
	if (player->isRemoved() || player->getHealth() <= 0) {
		if (recvbyte == CanaryLib::ClientEnterGame) {
			disconnect();
			return;
		}

		if (recvbyte != CanaryLib::ClientLeaveGame) {
			return;
		}
	}

	//modules system
	if (g_modules().eventOnRecvByte(player, recvbyte, input_msg)) {
		if (input_msg.hasOverflow()) {
			disconnect();
		}
		return;
	}

	switch (recvbyte) {
		case 0x14: logout(true, false); break;
		case 0x1D: g_game().playerReceivePingBack(player); break;
		case 0x1E: g_game().playerReceivePing(player); break;
		case 0x32: parseExtendedOpcode(); break; //otclient extended opcode
		case 0x64: parseAutoWalk(); break;
		case 0x65: g_game().playerMove(player, DIRECTION_NORTH); break;
		case 0x66: g_game().playerMove(player, DIRECTION_EAST); break;
		case 0x67: g_game().playerMove(player, DIRECTION_SOUTH); break;
		case 0x68: g_game().playerMove(player, DIRECTION_WEST); break;
		case 0x69: g_game().playerStopAutoWalk(player); break;
		case 0x6A: g_game().playerMove(player, DIRECTION_NORTHEAST); break;
		case 0x6B: g_game().playerMove(player, DIRECTION_SOUTHEAST); break;
		case 0x6C: g_game().playerMove(player, DIRECTION_SOUTHWEST); break;
		case 0x6D: g_game().playerMove(player, DIRECTION_NORTHWEST); break;
		case 0x6F: g_game().playerTurn(player, DIRECTION_NORTH); break;
		case 0x70: g_game().playerTurn(player, DIRECTION_EAST); break;
		case 0x71: g_game().playerTurn(player, DIRECTION_SOUTH); break;
		case 0x72: g_game().playerTurn(player, DIRECTION_WEST); break;
		case 0x73: parseTeleport(); break;
		case 0x77: parseEquipObject(); break;
		case 0x78: parseThrow(); break;
		case 0x79: parseLookInShop(); break;
		case 0x7A: parsePlayerPurchase(); break;
		case 0x7B: parsePlayerSale(); break;
		case 0x7C: g_game().playerCloseShop(player); break;
		case 0x7D: parseRequestTrade(); break;
		case 0x7E: parseLookInTrade(); break;
		case 0x7F: g_game().playerAcceptTrade(player); break;
		case 0x80: g_game().playerCloseTrade(player); break;
		case 0x82: parseUseItem(); break;
		case 0x83: parseUseItemEx(); break;
		case 0x84: parseUseWithCreature(); break;
		case 0x85: parseRotateItem(); break;
		case 0x87: parseCloseContainer(); break;
		case 0x88: parseUpArrowContainer(); break;
		case 0x89: parseTextWindow(); break;
		case 0x8A: parseHouseWindow(); break;
		case 0x8B: parseWrapableItem(); break;
		case 0x8C: parseLookAt(); break;
		case 0x8D: parseLookInBattleList(); break;
		case 0x8E: /* join aggression */ break;
		case 0x96: parseSay(); break;
		case 0x97: g_game().playerRequestChannels(player); break;
		case 0x98: parseOpenChannel(); break;
		case 0x99: parseCloseChannel(); break;
		case 0x9A: parseOpenPrivateChannel(); break;
		case 0x9E: g_game().playerCloseNpcChannel(player); break;
		case 0xA0: parseFightModes(); break;
		case 0xA1: parseAttack(); break;
		case 0xA2: parseFollow(); break;
		case 0xA3: parseInviteToParty(); break;
		case 0xA4: parseJoinParty(); break;
		case 0xA5: parseRevokePartyInvite(); break;
		case 0xA6: parsePassPartyLeadership(); break;
		case 0xA7: g_game().playerLeaveParty(player); break;
		case 0xA8: parseEnableSharedPartyExperience(); break;
		case 0xAA: g_game().playerCreatePrivateChannel(player); break;
		case 0xAB: parseChannelInvite(); break;
		case 0xAC: parseChannelExclude(); break;
		case 0xAD: parseCyclopediaHouseAction(); break;
		case 0xBE: g_game().playerCancelAttackAndFollow(player->getID()); break;
		case 0xC7: parseTournamentLeaderboard(); break;
		case 0xC9: /* update tile */ break;
		case 0xCA: parseUpdateContainer(); break;
		#if GAME_FEATURE_BROWSEFIELD > 0
		case 0xCB: parseBrowseField(); break;
		#endif
		#if GAME_FEATURE_CONTAINER_PAGINATION > 0
		case 0xCC: parseSeekInContainer(); break;
		#endif
		#if GAME_FEATURE_INSPECTION > 0
		case 0xCD: parseInspectionObject(); break;
		#endif
		#if GAME_FEATURE_QUEST_TRACKER > 0
		case 0xD0: parseTrackedQuestFlags(); break;
		#endif
		case 0xD2: g_game().playerRequestOutfit(player); break;
		case 0xD3: parseSetOutfit(); break;
		#if GAME_FEATURE_MOUNTS > 0
		case 0xD4: parseToggleMount(); break;
		#endif
		case 0xDC: parseAddVip(); break;
		case 0xDD: parseRemoveVip(); break;
		case 0xDE: parseEditVip(); break;
		case 0xE1: g_game().playerMonsterCyclopedia(player); break;
		case 0xE2: parseCyclopediaMonsters(); break;
		case 0xE3: parseCyclopediaRace(); break;
		case 0xE5: parseCyclopediaCharacterInfo(); break;
		case 0xE6: parseBugReport(); break;
		case 0xE7: /* thank you */ break;
		case 0xE8: parseDebugAssert(); break;
		case 0xF0: g_game().playerShowQuestLog(player); break;
		case 0xF1: parseQuestLine(); break;
		case 0xF2: parseRuleViolationReport(); break;
		case 0xF3: /* get object info */ break;
		#if GAME_FEATURE_MARKET > 0
		case 0xF4: parseMarketLeave(); break;
		case 0xF5: parseMarketBrowse(); break;
		case 0xF6: parseMarketCreateOffer(); break;
		case 0xF7: parseMarketCancelOffer(); break;
		case 0xF8: parseMarketAcceptOffer(); break;
		#endif
		case 0xF9: parseModalWindowAnswer(); break;

		default:
			// std::cout << "Player: " << player->getName() << " sent an unknown packet header: 0x" << std::hex << static_cast<uint16_t>(recvbyte) << std::dec << "!" << std::endl;
			break;
	}

	if (msg.hasOverflow()) {
		disconnect();
	}
}

void ProtocolGame::GetTileDescription(const Tile* tile)
{
	int32_t count;
	Item* ground = tile->getGround();
	if (ground) {
		AddItem(ground);
		count = 1;
	} else {
		count = 0;
	}

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			AddItem(*it);
			if (++count == 10) {
				break;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		bool playerAdded = false;
		if (count < 10) {
			for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
				const Creature* creature = (*it);
				if (!player->canSeeCreature(creature)) {
					continue;
				}

				if (creature->getID() == player->getID()) {
					playerAdded = true;
				}

				bool known;
				uint32_t removedKnown;
				checkCreatureAsKnown(creature->getID(), known, removedKnown);
				AddCreature(creature, known, removedKnown);
				if (++count == 10) {
					break;
				}
			}
		}
		if (!playerAdded && tile->getPosition() == player->getPosition()) {
			const Creature* creature = player;

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(creature, known, removedKnown);
		}
	}

	if (items && count < 10) {
		for (auto it = ItemVector::const_reverse_iterator(items->getEndDownItem()), end = ItemVector::const_reverse_iterator(items->getBeginDownItem()); it != end; ++it) {
			AddItem(*it);
			if (++count == 10) {
				return;
			}
		}
	}
}

void ProtocolGame::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height)
{
	int32_t skip = -1;
	int32_t startz, endz, zstep;
	if (z > 7) {
		startz = z - 2;
		endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, z + 2);
		zstep = 1;
	} else {
		startz = 7;
		endz = 0;
		zstep = -1;
	}

	for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
		GetFloorDescription(x, y, nz, width, height, z - nz, skip);
	}

	if (skip >= 0) {
		playermsg.writeByte(skip);
		playermsg.writeByte(0xFF);
	}
}

void ProtocolGame::GetFloorDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, int32_t offset, int32_t& skip)
{
	std::vector<Tile*> tileVector = g_game().map.getFloorTiles(x + offset, y + offset, width, height, z);
	for (Tile* tile : tileVector) {
		if (tile) {
			if (skip >= 0) {
				playermsg.writeByte(skip);
				playermsg.writeByte(0xFF);
			}

			skip = 0;
			GetTileDescription(tile);
		} else if (skip == 0xFE) {
			playermsg.writeByte(0xFF);
			playermsg.writeByte(0xFF);
			skip = -1;
		} else {
			++skip;
		}
	}
}

void ProtocolGame::checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown)
{
	auto result = knownCreatureSet.insert(id);
	if (!result.second) {
		known = true;
		return;
	}
	known = false;
	if (knownCreatureSet.size() > 1300) {
		// Look for a creature to remove
		#if GAME_FEATURE_PARTY_LIST > 0
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			// We need to protect party players from removing
			Creature* creature = g_game().getCreatureByID(*it);
			if (creature && creature->getPlayer()) {
				Player* checkPlayer = creature->getPlayer();
				if (player->getParty() != checkPlayer->getParty() && !canSee(creature)) {
					removedKnown = *it;
					knownCreatureSet.erase(it);
					return;
				}
			} else if (!canSee(creature)) {
				removedKnown = *it;
				knownCreatureSet.erase(it);
				return;
			}
		}
		#else
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			Creature* creature = g_game().getCreatureByID(*it);
			if (!canSee(creature)) {
				removedKnown = *it;
				knownCreatureSet.erase(it);
				return;
			}
		}
		#endif

		// Bad situation. Let's just remove anyone.
		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		removedKnown = *it;
		knownCreatureSet.erase(it);
	} else {
		removedKnown = 0;
	}
}

bool ProtocolGame::canSee(const Creature* c) const
{
	if (!c || !player || c->isRemoved()) {
		return false;
	}

	if (!player->canSeeCreature(c)) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolGame::canSee(const Position& pos) const
{
	return canSee(pos.x, pos.y, pos.z);
}

bool ProtocolGame::canSee(int32_t x, int32_t y, int32_t z) const
{
	if (!player) {
		return false;
	}

	const Position& myPos = player->getPosition();
	if (myPos.z <= 7) {
		//we are on ground level or above (7 -> 0)
		//view is from 7 -> 0
		if (z > 7) {
			return false;
		}
	} else if (myPos.z >= 8) {
		//we are underground (8 -> 15)
		//view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > 2) {
			return false;
		}
	}

	//negative offset means that the action taken place is on a lower floor than ourself
	int32_t offsetz = myPos.getZ() - z;
	if ((x >= myPos.getX() - (CLIENT_MAP_WIDTH_OFFSET - 1) + offsetz) && (x <= myPos.getX() + CLIENT_MAP_WIDTH_OFFSET + offsetz) &&
	        (y >= myPos.getY() - (CLIENT_MAP_HEIGHT_OFFFSET - 1) + offsetz) && (y <= myPos.getY() + CLIENT_MAP_HEIGHT_OFFFSET + offsetz)) {
		return true;
	}
	return false;
}

// Parse methods
void ProtocolGame::parseChannelInvite()
{
	const std::string name = input_msg.readString();
	if (!name.empty() && name.length() <= NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		g_game().playerChannelInvite(player, name);
	}
}

void ProtocolGame::parseChannelExclude()
{
	const std::string name = input_msg.readString();
	if (!name.empty() && name.length() <= NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		g_game().playerChannelExclude(player, name);
	}
}

void ProtocolGame::parseOpenChannel()
{
	uint16_t channelId = input_msg.read<uint16_t>();
	g_game().playerOpenChannel(player, channelId);
}

void ProtocolGame::parseCloseChannel()
{
	uint16_t channelId = input_msg.read<uint16_t>();
	g_game().playerCloseChannel(player, channelId);
}

void ProtocolGame::parseOpenPrivateChannel()
{
	std::string receiver = input_msg.readString();
	if (!receiver.empty() && receiver.length() <= NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		g_game().playerOpenPrivateChannel(player, receiver);
	}
}

#if GAME_FEATURE_QUEST_TRACKER > 0
void ProtocolGame::parseTrackedQuestFlags()
{
	std::vector<uint16_t> quests;
	uint8_t missions = input_msg.readByte();
	quests.resize(missions);
	for (uint8_t i = 0; i < missions; ++i) {
		quests[i] = input_msg.read<uint16_t>();
	}
	g_game().playerResetTrackedQuests(player, quests);
}
#endif

void ProtocolGame::parseAutoWalk()
{
	uint8_t numdirs = input_msg.readByte();
	if (numdirs == 0) {
		return;
	}

	std::vector<Direction> path;
	path.resize(numdirs, DIRECTION_NORTH);
	for (uint8_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = input_msg.readByte();
		switch (rawdir) {
			case 1: path[numdirs - i - 1] = DIRECTION_EAST; break;
			case 2: path[numdirs - i - 1] = DIRECTION_NORTHEAST; break;
			case 3: path[numdirs - i - 1] = DIRECTION_NORTH; break;
			case 4: path[numdirs - i - 1] = DIRECTION_NORTHWEST; break;
			case 5: path[numdirs - i - 1] = DIRECTION_WEST; break;
			case 6: path[numdirs - i - 1] = DIRECTION_SOUTHWEST; break;
			case 7: path[numdirs - i - 1] = DIRECTION_SOUTH; break;
			case 8: path[numdirs - i - 1] = DIRECTION_SOUTHEAST; break;
			default: break;
		}
	}
	g_game().playerAutoWalk(player->getID(), path);
}

void ProtocolGame::parseSetOutfit()
{
	// TODO: implement outfit type
	// uint8_t outfitType = 0;
	// outfitType = input_msg.readByte();

	Outfit_t newOutfit;
	#if GAME_FEATURE_LOOKTYPE_U16 > 0
	newOutfit.lookType = input_msg.read<uint16_t>();
	#else
	newOutfit.lookType = input_msg.readByte();
	#endif
	newOutfit.lookHead = input_msg.readByte();
	newOutfit.lookBody = input_msg.readByte();
	newOutfit.lookLegs = input_msg.readByte();
	newOutfit.lookFeet = input_msg.readByte();
	newOutfit.lookAddons = input_msg.readByte();

	#if GAME_FEATURE_MOUNTS > 0
	newOutfit.lookMount = input_msg.read<uint16_t>();
	#endif

	g_game().playerChangeOutfit(player, newOutfit);
}

#if GAME_FEATURE_MOUNTS > 0
void ProtocolGame::parseToggleMount()
{
	bool mount = input_msg.readByte() != 0;
	g_game().playerToggleMount(player, mount);
}
#endif

void ProtocolGame::parseUseItem()
{
	Position pos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t stackpos = input_msg.readByte();
	uint8_t index = input_msg.readByte();
	g_game().playerUseItem(player->getID(), pos, stackpos, index, spriteId);
}

void ProtocolGame::parseUseItemEx()
{
	Position fromPos = input_msg.getPosition();
	uint16_t fromSpriteId = input_msg.read<uint16_t>();
	uint8_t fromStackPos = input_msg.readByte();
	Position toPos = input_msg.getPosition();
	uint16_t toSpriteId = input_msg.read<uint16_t>();
	uint8_t toStackPos = input_msg.readByte();
	g_game().playerUseItemEx(player->getID(), fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
}

void ProtocolGame::parseUseWithCreature()
{
	Position fromPos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t fromStackPos = input_msg.readByte();
	uint32_t creatureId = input_msg.read<uint32_t>();
	g_game().playerUseWithCreature(player->getID(), fromPos, fromStackPos, creatureId, spriteId);
}

void ProtocolGame::parseCloseContainer()
{
	uint8_t cid = input_msg.readByte();
	g_game().playerCloseContainer(player, cid);
}

void ProtocolGame::parseUpArrowContainer()
{
	uint8_t cid = input_msg.readByte();
	g_game().playerMoveUpContainer(player, cid);
}

void ProtocolGame::parseUpdateContainer()
{
	uint8_t cid = input_msg.readByte();
	g_game().playerUpdateContainer(player, cid);
}

void ProtocolGame::parseThrow()
{
	Position fromPos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t fromStackpos = input_msg.readByte();
	Position toPos = input_msg.getPosition();
	uint8_t count = input_msg.readByte();
	if (toPos != fromPos) {
		g_game().playerMoveThing(player->getID(), fromPos, spriteId, fromStackpos, toPos, count);
	}
}

void ProtocolGame::parseWrapableItem()
{
	Position pos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t stackpos = input_msg.readByte();
	g_game().playerWrapableItem(player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseLookAt()
{
	Position pos = input_msg.getPosition();
	input_msg.skip(2); // spriteId
	uint8_t stackpos = input_msg.readByte();
	g_game().playerLookAt(player, pos, stackpos);
}

void ProtocolGame::parseLookInBattleList()
{
	uint32_t creatureId = input_msg.read<uint32_t>();
	g_game().playerLookInBattleList(player, creatureId);
}

void ProtocolGame::parseSay()
{
	std::string receiver;
	uint16_t channelId;

	SpeakClasses type = translateSpeakClassFromClient(input_msg.readByte());
	if (type == TALKTYPE_NONE) {
		return;
	}
	switch (type) {
		case TALKTYPE_PRIVATE_TO:
		case TALKTYPE_PRIVATE_RED_TO:
			receiver = input_msg.readString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_O:
		case TALKTYPE_CHANNEL_R1:
			channelId = input_msg.read<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	std::string text = input_msg.readString();
	trimString(text);
	if (text.empty() || text.length() > 255 || receiver.length() > NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		return;
	}

	g_game().playerSay(player, channelId, type, receiver, text);
}

void ProtocolGame::parseFightModes()
{
	uint8_t rawFightMode = input_msg.readByte(); // 1 - offensive, 2 - balanced, 3 - defensive
	uint8_t rawChaseMode = input_msg.readByte(); // 0 - stand while fightning, 1 - chase opponent
	uint8_t rawSecureMode = input_msg.readByte(); // 0 - can't attack unmarked, 1 - can attack unmarked
	// uint8_t rawPvpMode = input_msg.readByte(); // pvp mode introduced in 10.0

	fightMode_t fightMode;
	if (rawFightMode == 1) {
		fightMode = FIGHTMODE_ATTACK;
	} else if (rawFightMode == 2) {
		fightMode = FIGHTMODE_BALANCED;
	} else {
		fightMode = FIGHTMODE_DEFENSE;
	}

	g_game().playerSetFightModes(player, fightMode, (rawChaseMode != 0), (rawSecureMode != 0));
}

void ProtocolGame::parseAttack()
{
	uint32_t creatureId = input_msg.read<uint32_t>();
	g_game().playerSetAttackedCreature(player->getID(), creatureId);
}

void ProtocolGame::parseFollow()
{
	uint32_t creatureId = input_msg.read<uint32_t>();
	g_game().playerFollowCreature(player->getID(), creatureId);
}

void ProtocolGame::parseEquipObject()
{
	uint16_t spriteId = input_msg.read<uint16_t>();
	// input_msg.read<uint8_t>();

	g_game().playerEquipItem(player, spriteId);
}

void ProtocolGame::parseTeleport()
{
	Position position = input_msg.getPosition();
	g_game().playerTeleport(player, position);
}

void ProtocolGame::parseTextWindow()
{
	uint32_t windowTextId = input_msg.read<uint32_t>();
	const std::string newText = input_msg.readString();
	g_game().playerWriteItem(player, windowTextId, newText);
}

void ProtocolGame::parseHouseWindow()
{
	uint8_t doorId = input_msg.readByte();
	uint32_t id = input_msg.read<uint32_t>();
	const std::string text = input_msg.readString();
	g_game().playerUpdateHouseWindow(player, doorId, id, text);
}

void ProtocolGame::parseLookInShop()
{
	uint16_t id = input_msg.read<uint16_t>();
	uint8_t count = input_msg.readByte();
	g_game().playerLookInShop(player, id, count);
}

void ProtocolGame::parsePlayerPurchase()
{
	uint16_t id = input_msg.read<uint16_t>();
	uint8_t count = input_msg.readByte();
	uint8_t amount = input_msg.readByte();
	bool ignoreCap = (input_msg.readByte() != 0);
	bool inBackpacks = (input_msg.readByte() != 0);
	if (amount > 0 && amount <= 100) {
		g_game().playerPurchaseItem(player, id, count, amount, ignoreCap, inBackpacks);
	}
}

void ProtocolGame::parsePlayerSale()
{
	uint16_t id = input_msg.read<uint16_t>();
	uint8_t count = input_msg.readByte();
	uint8_t amount = input_msg.readByte();
	bool ignoreEquipped = (input_msg.readByte() != 0);
	if (amount > 0 && amount <= 100) {
		g_game().playerSellItem(player, id, count, amount, ignoreEquipped);
	}
}

void ProtocolGame::parseRequestTrade()
{
	Position pos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t stackpos = input_msg.readByte();
	uint32_t playerId = input_msg.read<uint32_t>();
	g_game().playerRequestTrade(player->getID(), pos, stackpos, playerId, spriteId);
}

void ProtocolGame::parseLookInTrade()
{
	bool counterOffer = (input_msg.readByte() == 0x01);
	uint8_t index = input_msg.readByte();
	g_game().playerLookInTrade(player, counterOffer, index);
}

void ProtocolGame::parseAddVip()
{
	const std::string name = input_msg.readString();
	if (!name.empty() && name.length() <= NETWORKMESSAGE_PLAYERNAME_MAXLENGTH) {
		g_game().playerRequestAddVip(player, name);
	}
}

void ProtocolGame::parseRemoveVip()
{
	uint32_t guid = input_msg.read<uint32_t>();
	g_game().playerRequestRemoveVip(player, guid);
}

void ProtocolGame::parseEditVip()
{
	uint32_t guid = input_msg.read<uint32_t>();
	const std::string description = input_msg.readString();
	uint32_t icon = std::min<uint32_t>(10, input_msg.read<uint32_t>()); // 10 is max icon in 9.63
	bool notify = (input_msg.readByte() != 0);
	g_game().playerRequestEditVip(player, guid, description, icon, notify);
}

void ProtocolGame::parseRotateItem()
{
	Position pos = input_msg.getPosition();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint8_t stackpos = input_msg.readByte();
	g_game().playerRotateItem(player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseRuleViolationReport()
{
	uint8_t reportType = input_msg.readByte();
	uint8_t reportReason = input_msg.readByte();
	const std::string targetName = input_msg.readString();
	const std::string comment = input_msg.readString();
	std::string translation;
	if (reportType == REPORT_TYPE_NAME) {
		translation = input_msg.readString();
	} else if (reportType == REPORT_TYPE_STATEMENT) {
		translation = input_msg.readString();
		input_msg.read<uint32_t>(); // statement id, used to get whatever player have said, we don't log that.
	}

	g_game().playerReportRuleViolation(player, targetName, reportType, reportReason, comment, translation);
}

void ProtocolGame::parseCyclopediaMonsters()
{
	std::string race;
	uint8_t type = input_msg.readByte();
	if(type != 0)
		return;

	race = input_msg.readString();

	g_game().playerCyclopediaMonsters(player, race);
}

void ProtocolGame::parseCyclopediaRace()
{
	uint16_t monsterId = input_msg.read<uint16_t>();
	g_game().playerCyclopediaRace(player, monsterId);
}

void ProtocolGame::parseCyclopediaHouseAction()
{
	// Testing purposes - do not write code this way, it has race condition
	// but for testing purpose I don't need 100% thread-safety
	(void)input_msg;
	/*uint8_t houseActionType = input_msg.readByte();
	switch (houseActionType) {
		case 0: {
			std::string housePage = input_msg.readString();
			std::cout << "Test[0]:" << std::endl;
			std::cout << "String[1]: " << housePage << std::endl;
			if (housePage == "Rathleton") {
				NetworkMessage outMsg;
				outmsg.writeByte(0xC7);
				outmsg.write<uint16_t>(0x01);
				outmsg.write<uint32_t>(0x4A4A);
				outmsg.writeByte(1);
				outmsg.writeByte(1);
				writeToOutputBuffer(outMsg);
			} else {
				NetworkMessage outMsg;
				outmsg.writeByte(0xC7);
				outmsg.write<uint16_t>(0x01);
				outmsg.write<uint32_t>(0x4A4A);
				outmsg.writeByte(1);
				outmsg.writeByte(2);
				outmsg.writeString("Test");
				outmsg.write<uint32_t>(0xFFFFFFFF);
				outmsg.writeByte(1);
				outmsg.writeByte(0);
				outmsg.writeByte(0);
				writeToOutputBuffer(outMsg);
			}
			break;
		}
		case 1: {
			std::cout << "Test[1]:" << std::endl;
			std::cout << "U32[1]: " << input_msg.read<uint32_t>() << std::endl;
			std::cout << "U64[2]: " << input_msg.read<uint64_t>() << std::endl;
			break;
		}
		case 2: {
			std::cout << "Test[2]:" << std::endl;
			std::cout << "U32[1]: " << input_msg.read<uint32_t>() << std::endl;
			std::cout << "U32[2]: " << input_msg.read<uint32_t>() << std::endl;
			break;
		}
		case 3: {
			std::cout << "Test[3]:" << std::endl;
			std::cout << "U32[1]: " << input_msg.read<uint32_t>() << std::endl;
			std::cout << "U32[2]: " << input_msg.read<uint32_t>() << std::endl;
			std::cout << "String[3]: " << input_msg.readString() << std::endl;
			std::cout << "U64[4]: " << input_msg.read<uint64_t>() << std::endl;
			break;
		}
	}*/
}

void ProtocolGame::parseCyclopediaCharacterInfo()
{
	CyclopediaCharacterInfoType_t characterInfoType;
	input_msg.read<uint32_t>();
	characterInfoType = static_cast<CyclopediaCharacterInfoType_t>(input_msg.readByte());

	g_game().playerCyclopediaCharacterInfo(player, characterInfoType);
}

void ProtocolGame::parseTournamentLeaderboard()
{
	uint8_t ledaerboardType = input_msg.readByte();
	if(ledaerboardType == 0)
	{
		const std::string worldName = input_msg.readString();
		uint16_t currentPage = input_msg.read<uint16_t>();
		(void)worldName;
		(void)currentPage;
	}
	else if(ledaerboardType == 1)
	{
		const std::string worldName = input_msg.readString();
		const std::string characterName = input_msg.readString();
		(void)worldName;
		(void)characterName;
	}
	uint8_t elementsPerPage = input_msg.readByte();
	(void)elementsPerPage;

	g_game().playerTournamentLeaderboard(player, ledaerboardType);
}

void ProtocolGame::parseBugReport()
{
	uint8_t category = input_msg.readByte();
	std::string message = input_msg.readString();

	Position position;
	if (category == BUG_CATEGORY_MAP) {
		position = input_msg.getPosition();
	}

	g_game().playerReportBug(player, message, position, category);
}

void ProtocolGame::parseDebugAssert()
{
	if (debugAssertSent) {
		return;
	}

	debugAssertSent = true;

	std::string assertLine = input_msg.readString();
	std::string date = input_msg.readString();
	std::string description = input_msg.readString();
	std::string comment = input_msg.readString();
	g_game().playerDebugAssert(player, assertLine, date, description, comment);
}

void ProtocolGame::parseInviteToParty()
{
	uint32_t targetId = input_msg.read<uint32_t>();
	g_game().playerInviteToParty(player, targetId);
}

void ProtocolGame::parseJoinParty()
{
	uint32_t targetId = input_msg.read<uint32_t>();
	g_game().playerJoinParty(player, targetId);
}

void ProtocolGame::parseRevokePartyInvite()
{
	uint32_t targetId = input_msg.read<uint32_t>();
	g_game().playerRevokePartyInvitation(player, targetId);
}

void ProtocolGame::parsePassPartyLeadership()
{
	uint32_t targetId = input_msg.read<uint32_t>();
	g_game().playerPassPartyLeadership(player, targetId);
}

void ProtocolGame::parseEnableSharedPartyExperience()
{
	bool sharedExpActive = (input_msg.readByte() == 1);
	g_game().playerEnableSharedPartyExperience(player, sharedExpActive);
}

void ProtocolGame::parseQuestLine()
{
	uint16_t questId = input_msg.read<uint16_t>();
	g_game().playerShowQuestLine(player, questId);
}

#if GAME_FEATURE_MARKET > 0
void ProtocolGame::parseMarketLeave()
{
	g_game().playerLeaveMarket(player);
}

void ProtocolGame::parseMarketBrowse()
{
	uint16_t browseId = input_msg.read<uint16_t>();
	if (browseId == MARKETREQUEST_OWN_OFFERS) {
		g_game().playerBrowseMarketOwnOffers(player);
	} else if (browseId == MARKETREQUEST_OWN_HISTORY) {
		g_game().playerBrowseMarketOwnHistory(player);
	} else {
		g_game().playerBrowseMarket(player, browseId);
	}
}

void ProtocolGame::parseMarketCreateOffer()
{
	uint8_t type = input_msg.readByte();
	uint16_t spriteId = input_msg.read<uint16_t>();
	uint16_t amount = input_msg.read<uint16_t>();
	uint32_t price = input_msg.read<uint32_t>();
	bool anonymous = (input_msg.readByte() != 0);
	if (amount > 0 && amount <= 64000 && price > 0 && price <= 999999999 && (type == MARKETACTION_BUY || type == MARKETACTION_SELL)) {
		g_game().playerCreateMarketOffer(player, type, spriteId, amount, price, anonymous);
	}
}

void ProtocolGame::parseMarketCancelOffer()
{
	uint32_t timestamp = input_msg.read<uint32_t>();
	uint16_t counter = input_msg.read<uint16_t>();
	g_game().playerCancelMarketOffer(player, timestamp, counter);
}

void ProtocolGame::parseMarketAcceptOffer()
{
	uint32_t timestamp = input_msg.read<uint32_t>();
	uint16_t counter = input_msg.read<uint16_t>();
	uint16_t amount = input_msg.read<uint16_t>();
	if (amount > 0 && amount <= 64000) {
		g_game().playerAcceptMarketOffer(player, timestamp, counter, amount);
	}
}
#endif

void ProtocolGame::parseModalWindowAnswer()
{
	uint32_t id = input_msg.read<uint32_t>();
	uint8_t button = input_msg.readByte();
	uint8_t choice = input_msg.readByte();
	g_game().playerAnswerModalWindow(player, id, button, choice);
}

#if GAME_FEATURE_BROWSEFIELD > 0
void ProtocolGame::parseBrowseField()
{
	const Position& pos = input_msg.getPosition();
	g_game().playerBrowseField(player->getID(), pos);
}
#endif

#if GAME_FEATURE_CONTAINER_PAGINATION > 0
void ProtocolGame::parseSeekInContainer()
{
	uint8_t containerId = input_msg.readByte();
	uint16_t index = input_msg.read<uint16_t>();
	g_game().playerSeekInContainer(player, containerId, index);
}
#endif

#if GAME_FEATURE_INSPECTION > 0
void ProtocolGame::parseInspectionObject()
{
	uint8_t inspectionType = input_msg.readByte();
	if(inspectionType == INSPECT_NORMALOBJECT)
	{
		Position pos = input_msg.getPosition();
		g_game().playerInspectItem(player, pos);
	}
	else if(inspectionType == INSPECT_NPCTRADE || inspectionType == INSPECT_CYCLOPEDIA)
	{
		uint16_t itemId = input_msg.read<uint16_t>();
		uint16_t itemCount = input_msg.readByte();
		g_game().playerInspectItem(player, itemId, itemCount, (inspectionType == INSPECT_CYCLOPEDIA));
	}
}
#endif

// Send methods
#if GAME_FEATURE_INSPECTION > 0
void ProtocolGame::sendItemInspection(uint16_t itemId, uint8_t itemCount, const Item* item, bool cyclopedia)
{
	playermsg.reset();
	playermsg.writeByte(0x76);
	playermsg.writeByte(0x00);//item
	playermsg.writeByte(cyclopedia ? 0x01 : 0x00);
	playermsg.writeByte(0x01);

	const ItemType& it = Item::items.getItemIdByClientId(itemId);

	if (item) {
		playermsg.writeString(item->getName());
		AddItem(item);
	} else {
		playermsg.writeString(it.name);
		AddItem(it.id, itemCount);
	}
	playermsg.writeByte(0); // imbuements

	auto descriptions = Item::getDescriptions(it, item);
	playermsg.writeByte(descriptions.size());
	for (const auto& description : descriptions) {
		playermsg.writeString(description.first);
		playermsg.writeString(description.second);
	}
	writeToOutputBuffer();
}
#endif

void ProtocolGame::sendOpenPrivateChannel(const std::string& receiver)
{
	playermsg.reset();
	playermsg.writeByte(0xAD);
	playermsg.writeString(receiver);
	writeToOutputBuffer();
}

#if GAME_FEATURE_CHAT_PLAYERLIST > 0
void ProtocolGame::sendChannelEvent(uint16_t channelId, const std::string& playerName, ChannelEvent_t channelEvent)
{
	playermsg.reset();
	playermsg.writeByte(0xF3);
	playermsg.write<uint16_t>(channelId);
	playermsg.writeString(playerName);
	playermsg.writeByte(channelEvent);
	writeToOutputBuffer();
}
#endif

void ProtocolGame::sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit)
{
	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x8E);
	playermsg.write<uint32_t>(creature->getID());
	AddOutfit(outfit);
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureLight(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	AddCreatureLight(creature);
	writeToOutputBuffer();
}

void ProtocolGame::sendWorldLight(LightInfo lightInfo)
{
	playermsg.reset();
	AddWorldLight(lightInfo);
	writeToOutputBuffer();
}

void ProtocolGame::sendTibiaTime(int32_t time)
{
	playermsg.reset();
	playermsg.writeByte(0xEF);
	playermsg.writeByte(time / 60);
	playermsg.writeByte(time % 60);
	writeToOutputBuffer();
}

void ProtocolGame::updateCreatureData(const Creature* creature)
{
	uint32_t cid = creature->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		return;
	}

	OperatingSystem_t regularOS = player->getOperatingSystem();
	OperatingSystem_t tfcOS = player->getTfcOperatingSystem();
	if ((regularOS >= CLIENTOS_NEW_LINUX && regularOS < CLIENTOS_OTCLIENT_LINUX) || tfcOS >= CLIENTOS_TFC_ANDROID) {
		//Using some hack so that I'm don't need to modify AddCreature function
		playermsg.reset();
		playermsg.setBufferPosition(-1);
		AddCreature(creature, false, cid);
		playermsg.setBufferPosition(0);
		playermsg.writeByte(0x03);
		playermsg.setLength(playermsg.getLength() - 2);
		writeToOutputBuffer();
	} else {
		if (canSee(creature)) {
			int32_t stackpos = creature->getTile()->getStackposOfCreature(player, creature);
			if (stackpos != -1) {
				playermsg.reset();
				playermsg.writeByte(0x6B);
				playermsg.addPosition(creature->getPosition());
				playermsg.writeByte(stackpos);
				AddCreature(creature, false, cid);
				writeToOutputBuffer();
				return;
			}
		}

		//Not the best choice we have here but let's update our creature
		const Position& pos = player->getPosition();
		playermsg.reset();
		playermsg.writeByte(0x6A);
		playermsg.addPosition(pos);
		#if GAME_FEATURE_TILE_ADDTHING_STACKPOS > 0
		playermsg.writeByte(0xFF);
		#endif
		AddCreature(creature, false, cid);
		playermsg.writeByte(0x69);
		playermsg.addPosition(pos);
		Tile* tile = player->getTile();
		if (tile) {
			GetTileDescription(tile);
			playermsg.writeByte(0x00);
			playermsg.writeByte(0xFF);
		} else {
			playermsg.writeByte(0x01);
			playermsg.writeByte(0xFF);
		}
		writeToOutputBuffer();
	}
}

void ProtocolGame::sendCreatureWalkthrough(const Creature* creature, bool walkthrough)
{
	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x92);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(walkthrough ? 0x00 : 0x01);
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureShield(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x91);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(player->getPartyShield(creature->getPlayer()));
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureSkull(const Creature* creature)
{
	if (g_game().getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x90);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(player->getSkullClient(creature));
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureType(const Creature* creature, uint8_t creatureType)
{
	playermsg.reset();
	playermsg.writeByte(0x95);
	playermsg.write<uint32_t>(creature->getID());
	if (creatureType == CREATURETYPE_SUMMON_OTHERS) {
		creatureType = CREATURETYPE_SUMMON_OWN;
	}
	playermsg.writeByte(creatureType);
	if (creatureType == CREATURETYPE_SUMMON_OWN) {
		const Creature* master = creature->getMaster();
		if (master) {
			playermsg.write<uint32_t>(master->getID());
		} else {
			playermsg.write<uint32_t>(0);
		}
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureSquare(const Creature* creature, Color_t color)
{
	if (!canSee(creature)) {
		return;
	}

	playermsg.reset();
	#if GAME_FEATURE_CREATURE_MARK > 0
	playermsg.writeByte(0x93);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(0x01);
	playermsg.writeByte(color);
	#else
	playermsg.writeByte(0x86);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(color);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendTutorial(uint8_t tutorialId)
{
	playermsg.reset();
	playermsg.writeByte(0xDC);
	playermsg.writeByte(tutorialId);
	writeToOutputBuffer();
}

void ProtocolGame::sendAddMarker(const Position& pos, uint8_t markType, const std::string& desc)
{
	playermsg.reset();
	playermsg.writeByte(0xDD);
	playermsg.writeByte(0x00);
	playermsg.addPosition(pos);
	playermsg.writeByte(markType);
	playermsg.writeString(desc);
	writeToOutputBuffer();
}

void ProtocolGame::sendMonsterCyclopedia()
{
	playermsg.reset();
	playermsg.writeByte(0xD5);

	auto races = g_monsters().getRaces();
	auto monsterRaces = g_monsters().getMonsterRaces();

	playermsg.write<uint16_t>(races.size());
	for (const auto& race : races) {
		playermsg.writeString(race.first);

		auto it = monsterRaces.find(race.second);
		if (it != monsterRaces.end()) {
			playermsg.write<uint16_t>(it->second.size());
			playermsg.write<uint16_t>(it->second.size());
		} else {
			playermsg.write<uint16_t>(0);
			playermsg.write<uint16_t>(0);
		}
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaMonsters(const std::string& race)
{
	playermsg.reset();
	playermsg.writeByte(0xD6);

	auto races = g_monsters().getRaces();
	auto monsterRaces = g_monsters().getMonsterRaces();
	playermsg.writeString(race);

	auto it = races.find(race);
	if (it != races.end()) {
		auto it2 = monsterRaces.find(it->second);
		if (it2 != monsterRaces.end()) {
			playermsg.write<uint16_t>(it2->second.size());
			for (const auto& monster : it2->second) {
				uint8_t monsterProgress = BESTIARY_PROGRESS_COMPLETED;

				playermsg.write<uint16_t>(monster.first);
				playermsg.writeByte(monsterProgress);
				if (monsterProgress != BESTIARY_PROGRESS_NONE) {
					playermsg.writeByte(BESTIARY_OCCURENCE_COMMON);
				}
			}
		} else {
			playermsg.write<uint16_t>(0);
		}
	} else {
		playermsg.write<uint16_t>(0);
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaRace(uint16_t monsterId)
{
	playermsg.reset();
	playermsg.writeByte(0xD7);

	auto monsterRaces = g_monsters().getMonsterRaces();
	for (const auto& race : monsterRaces) {
		auto it = race.second.find(monsterId);
		if (it != race.second.end()) {
			MonsterType* monsterType = g_monsters().getMonsterType(it->second);
			if (monsterType) {
				uint8_t monsterProgress = BESTIARY_PROGRESS_COMPLETED;

				playermsg.write<uint16_t>(monsterId);
				playermsg.writeString(g_monsters().getRaceName(race.first));
				playermsg.writeByte(monsterProgress);
				playermsg.write<uint32_t>(0); // total kills
				playermsg.write<uint16_t>(0); // kills to progress 1
				playermsg.write<uint16_t>(0); // kills to progress 2
				playermsg.write<uint16_t>(0); // kills to progress 3
				if (monsterProgress >= BESTIARY_PROGRESS_FIRST) {
					playermsg.writeByte(BESTIARY_DIFFICULTY_HARMLESS);
					playermsg.writeByte(BESTIARY_OCCURENCE_COMMON);

					std::vector<const std::vector<LootBlock>*> lootBlocks{ &monsterType->info.lootItems };
					uint8_t lootSize = 0;
					auto startLoot = playermsg.getBufferPosition();
					playermsg.writeByte(lootSize);
					for (std::vector<const std::vector<LootBlock>*>::iterator lit = lootBlocks.begin(); lit != lootBlocks.end(); lit++) {
						const std::vector<LootBlock>* lootVector = (*lit);
						for (const auto& lootBlock : *lootVector) {
							if (!lootBlock.childLoot.empty()) {
								lootBlocks.push_back(&lootBlock.childLoot);
							} else {
								uint16_t itemId = lootBlock.id;

								const ItemType& item = Item::items[itemId];
								playermsg.write<uint16_t>(item.clientId);
								if (lootBlock.chance >= 25000) {
									playermsg.writeByte(BESTIARY_RARITY_COMMON);
								} else if (lootBlock.chance >= 5000) {
									playermsg.writeByte(BESTIARY_RARITY_UNCOMMON);
								} else if (lootBlock.chance >= 1000) {
									playermsg.writeByte(BESTIARY_RARITY_SEMIRARE);
								} else if (lootBlock.chance >= 500) {
									playermsg.writeByte(BESTIARY_RARITY_RARE);
								} else {
									playermsg.writeByte(BESTIARY_RARITY_VERYRARE);
								}
								playermsg.writeByte(0x00); // special event item
								if (itemId != 0) { // 0 indicate hidden item
									playermsg.writeString(item.name);
									playermsg.writeByte((lootBlock.countmax > 1) ? 0x01 : 0x00);
								}

								if (++lootSize == 0xFF) {
									goto EndLoot;
								}
							}
						}
					}

					EndLoot:
					auto returnTo = playermsg.getBufferPosition();
					playermsg.setBufferPosition(startLoot);
					playermsg.writeByte(lootSize);
					playermsg.setLength(playermsg.getLength() - 1); // decrease one extra bytes we made
					playermsg.setBufferPosition(returnTo);
				}
				if (monsterProgress >= BESTIARY_PROGRESS_SECOND) {
					playermsg.write<uint16_t>(0); // charm points
					if (!monsterType->info.isHostile) {
						playermsg.writeByte(BESTIARY_ATTACKTYPE_NONE);
					} else if (monsterType->info.targetDistance > 1) {
						playermsg.writeByte(BESTIARY_ATTACKTYPE_DISTANCE);
					} else {
						playermsg.writeByte(BESTIARY_ATTACKTYPE_MELEE);
					}
					if (!monsterType->info.attackSpells.empty() || !monsterType->info.defenseSpells.empty()) {
						playermsg.writeByte(0x01); // casts spells
					} else {
						playermsg.writeByte(0x00); // casts spells
					}
					playermsg.write<uint32_t>(static_cast<uint32_t>(monsterType->info.healthMax));
					playermsg.write<uint32_t>(static_cast<uint32_t>(monsterType->info.experience));
					playermsg.write<uint16_t>(static_cast<uint16_t>(monsterType->info.baseSpeed / 2));
					playermsg.write<uint16_t>(static_cast<uint16_t>(monsterType->info.armor));
				}
				if (monsterProgress >= BESTIARY_PROGRESS_THIRD) {
					playermsg.writeByte(8); // combats

					static const CombatType_t combats[] = {COMBAT_PHYSICALDAMAGE, COMBAT_FIREDAMAGE, COMBAT_EARTHDAMAGE, COMBAT_ENERGYDAMAGE, COMBAT_ICEDAMAGE, COMBAT_HOLYDAMAGE, COMBAT_DEATHDAMAGE, COMBAT_HEALING};
					for (std::underlying_type<Cipbia_Elementals_t>::type i = CIPBIA_ELEMENTAL_PHYSICAL; i <= CIPBIA_ELEMENTAL_HEALING; i++) {
						playermsg.writeByte(i);

						auto combat = combats[i];
						if (monsterType->info.damageImmunities & combat) {
							playermsg.write<int16_t>(0);
						} else {
							auto combatDmg = monsterType->info.elementMap.find(combats[i]);
							if (combatDmg != monsterType->info.elementMap.end()) {
								playermsg.write<int16_t>(100-combatDmg->second);
							} else {
								playermsg.write<int16_t>(100);
							}
						}
					}

					playermsg.write<uint16_t>(1); // locations
					playermsg.writeString(""); // location - TODO
				}
				if (monsterProgress >= BESTIARY_PROGRESS_COMPLETED) {
					bool monsterHaveActiveCharm = false;
					playermsg.writeByte((monsterHaveActiveCharm ? 0x01 : 0x00));
					if (monsterHaveActiveCharm) {
						playermsg.writeByte(0); // ??
						playermsg.write<uint32_t>(0); // ??
					} else {
						playermsg.writeByte(0); // ??
					}
				}
				writeToOutputBuffer();
				return;
			}
		}
	}

	playermsg.write<uint16_t>(monsterId);
	playermsg.writeString("Extra Dimensional");
	playermsg.writeByte(BESTIARY_PROGRESS_NONE);
	playermsg.write<uint32_t>(0); // total kills
	playermsg.write<uint16_t>(0); // kills to progress 1
	playermsg.write<uint16_t>(0); // kills to progress 2
	playermsg.write<uint16_t>(0); // kills to progress 3
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaBonusEffects()
{
	playermsg.reset();
	playermsg.writeByte(0xD8);
	playermsg.write<int32_t>(0); // charm points
	playermsg.writeByte(0); // charms
	//playermsg.writeByte(10); // charmid
	//playermsg.writeString("Cripple"); // charmname
	//playermsg.writeString("something"); // charmdescription
	//playermsg.writeByte(0); // ??
	//playermsg.write<uint16_t>(500); // charm price
	//playermsg.writeByte(0); // unlocked
	//playermsg.writeByte(1); // activated
	// if activated
	//playermsg.write<uint16_t>(78); // monster id
	//playermsg.write<uint32_t>(1000); // clear price

	playermsg.writeByte(0); // remaining assignable charms
	playermsg.write<uint16_t>(0); // assignable monsters
	//playermsg.write<uint16_t>(78); // monster id
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterBaseInformation()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_BASEINFORMATION);
	playermsg.writeByte(0x00); // No data available
	playermsg.writeString(player->getName());
	playermsg.writeString(player->getVocation()->getVocName());
	playermsg.write<uint16_t>(player->getLevel());
	AddOutfit(player->getDefaultOutfit());

	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // enable store summary & character titles
	playermsg.writeString(""); // character title
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterGeneralStats()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_GENERALSTATS);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint64_t>(player->getExperience());
	playermsg.write<uint16_t>(player->getLevel());
	playermsg.writeByte(player->getLevelPercent());
	playermsg.write<uint16_t>(100); // base xp gain rate
	playermsg.write<int32_t>(0); // tournament xp factor
	playermsg.write<uint16_t>(0); // low level bonus
	playermsg.write<uint16_t>(0); // xp boost
	playermsg.write<uint16_t>(100); // stamina multiplier (100 = x1.0)
	playermsg.write<uint16_t>(0); // xpBoostRemainingTime
	playermsg.writeByte(0x00); // canBuyXpBoost
	playermsg.write<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));
	playermsg.writeByte(player->getSoul());
	playermsg.write<uint16_t>(player->getStaminaMinutes());

	Condition* condition = player->getCondition(CONDITION_REGENERATION);
	playermsg.write<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);
	playermsg.write<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);
	playermsg.write<uint16_t>(player->getSpeed() / 2);
	playermsg.write<uint16_t>(player->getBaseSpeed() / 2);
	playermsg.write<uint32_t>(player->getCapacity());
	playermsg.write<uint32_t>(player->getCapacity());
	playermsg.write<uint32_t>(player->getFreeCapacity());
	playermsg.writeByte(8); // Skills count
	playermsg.writeByte(1); // Magic Level hardcoded skill id
	playermsg.write<uint16_t>(player->getMagicLevel());
	playermsg.write<uint16_t>(player->getBaseMagicLevel());
	playermsg.write<uint16_t>(player->getBaseMagicLevel());//loyalty bonus
	playermsg.write<uint16_t>(player->getMagicLevelPercent() * 100);
	for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { //TODO: check if all clients have the same hardcoded skill ids
		static const uint8_t HardcodedSkillIds[] = {11, 9, 8, 10, 7, 6, 13};
		playermsg.writeByte(HardcodedSkillIds[i]);
		playermsg.write<uint16_t>(std::min<int32_t>(player->getSkillLevel(i), std::numeric_limits<uint16_t>::max()));
		playermsg.write<uint16_t>(player->getBaseSkill(i));
		playermsg.write<uint16_t>(player->getBaseSkill(i));//loyalty bonus
		playermsg.write<uint16_t>(player->getSkillPercent(i) * 100);
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterCombatStats()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_COMBATSTATS);
	playermsg.writeByte(0x00); // No data available
	for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
		playermsg.write<uint16_t>(std::min<int32_t>(100, player->varSpecialSkills[i]));
		playermsg.write<uint16_t>(0);
	}
	uint8_t haveBlesses = 0;
	uint8_t blessings = 8;
	for (uint8_t i = 1; i < blessings; i++) {
		if (player->hasBlessing(i)) {
			haveBlesses++;
		}
	}
	playermsg.writeByte(haveBlesses);
	playermsg.writeByte(blessings);
	playermsg.write<uint16_t>(0); // attackValue
	playermsg.writeByte(0); // damageType
	playermsg.writeByte(0); // convertedDamage
	playermsg.writeByte(0); // convertedType
	playermsg.write<uint16_t>(0); // armorValue
	playermsg.write<uint16_t>(0); // defenseValue
	playermsg.writeByte(0); // combats
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterRecentDeaths()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_RECENTDEATHS);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint16_t>(0); // current page
	playermsg.write<uint16_t>(0); // available pages
	playermsg.write<uint16_t>(0); // deaths
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterRecentPvPKills()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_RECENTPVPKILLS);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint16_t>(0); // current page
	playermsg.write<uint16_t>(0); // available pages
	playermsg.write<uint16_t>(0); // kills
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterAchievements()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_ACHIEVEMENTS);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint16_t>(0); // total points
	playermsg.write<uint16_t>(0); // total secret achievements
	playermsg.write<uint16_t>(0); // achievements
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterItemSummary()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_ITEMSUMMARY);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint16_t>(0); // ??
	playermsg.write<uint16_t>(0); // ??
	playermsg.write<uint16_t>(0); // ??
	playermsg.write<uint16_t>(0); // ??
	playermsg.write<uint16_t>(0); // ??
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterOutfitsMounts()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_OUTFITSMOUNTS);
	playermsg.writeByte(0x00); // No data available
	Outfit_t currentOutfit = player->getDefaultOutfit();

	uint16_t outfitSize = 0;
	auto startOutfits = playermsg.getBufferPosition();
	playermsg.write<uint16_t>(outfitSize);

	const auto& outfits = Outfits::getInstance().getOutfits(player->getSex());
	for (const Outfit& outfit : outfits) {
		uint8_t addons;
		if (!player->getOutfitAddons(outfit, addons)) {
			continue;
		}
		outfitSize++;

		playermsg.write<uint16_t>(outfit.lookType);
		playermsg.writeString(outfit.name);
		playermsg.writeByte(addons);
		playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_NONE);
		if (outfit.lookType == currentOutfit.lookType) {
			playermsg.write<uint32_t>(1000);
		} else {
			playermsg.write<uint32_t>(0);
		}
	}
	if (outfitSize > 0) {
		playermsg.writeByte(currentOutfit.lookHead);
		playermsg.writeByte(currentOutfit.lookBody);
		playermsg.writeByte(currentOutfit.lookLegs);
		playermsg.writeByte(currentOutfit.lookFeet);
	}

	uint16_t mountSize = 0;
	auto startMounts = playermsg.getBufferPosition();
	playermsg.write<uint16_t>(mountSize);
	for (const Mount& mount : g_game().mounts.getMounts()) {
		#if GAME_FEATURE_MOUNTS > 0
		if (player->hasMount(&mount)) {
		#else
		if (true) {
		#endif
			mountSize++;

			playermsg.write<uint16_t>(mount.clientId);
			playermsg.writeString(mount.name);
			playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_OUTFITTYPE_NONE);
			playermsg.write<uint32_t>(1000);
		}
	}

	playermsg.setBufferPosition(startOutfits);
	playermsg.write<uint16_t>(outfitSize);
	playermsg.setBufferPosition(startMounts);
	playermsg.write<uint16_t>(mountSize);
	playermsg.setLength(playermsg.getLength() - 4); // decrease four extra bytes we made
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterStoreSummary()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_STORESUMMARY);
	playermsg.writeByte(0x00); // No data available
	playermsg.write<uint32_t>(0); // ??
	playermsg.write<uint32_t>(0); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	playermsg.write<uint16_t>(0); // ??
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterInspection()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_INSPECTION);
	playermsg.writeByte(0x00); // No data available
	uint8_t inventoryItems = 0;
	auto startInventory = playermsg.getBufferPosition();
	playermsg.writeByte(inventoryItems);
	for (std::underlying_type<slots_t>::type slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; slot++) {
		Item* inventoryItem = player->getInventoryItem(static_cast<slots_t>(slot));
		if (inventoryItem) {
			inventoryItems++;

			playermsg.writeByte(slot);
			playermsg.writeString(inventoryItem->getName());
			AddItem(inventoryItem);
			playermsg.writeByte(0); // imbuements

			auto descriptions = Item::getDescriptions(Item::items[inventoryItem->getID()], inventoryItem);
			playermsg.writeByte(descriptions.size());
			for (const auto& description : descriptions) {
				playermsg.writeString(description.first);
				playermsg.writeString(description.second);
			}
		}
	}
	playermsg.writeString(player->getName());
	AddOutfit(player->getDefaultOutfit());

	playermsg.writeByte(3);
	playermsg.writeString("Level");
	playermsg.writeString(std::to_string(player->getLevel()));
	playermsg.writeString("Vocation");
	playermsg.writeString(player->getVocation()->getVocName());
	playermsg.writeString("Outfit");

	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(player->getSex(), player->getDefaultOutfit().lookType);
	if (outfit) {
		playermsg.writeString(outfit->name);
	} else {
		playermsg.writeString("unknown");
	}
	playermsg.setBufferPosition(startInventory);
	playermsg.writeByte(inventoryItems);
	playermsg.setLength(playermsg.getLength() - 1); // decrease one extra byte we made
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterBadges()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_BADGES);
	playermsg.writeByte(0x00); // No data available
	playermsg.writeByte(0x00); // enable badges
	writeToOutputBuffer();
}

void ProtocolGame::sendCyclopediaCharacterTitles()
{
	playermsg.reset();
	playermsg.writeByte(0xDA);
	playermsg.writeByte(CYCLOPEDIA_CHARACTERINFO_TITLES);
	playermsg.writeByte(0x00); // No data available
	playermsg.writeByte(0x00); // ??
	playermsg.writeByte(0x00); // ??
	writeToOutputBuffer();
}

void ProtocolGame::sendTournamentLeaderboard()
{
	playermsg.reset();
	playermsg.writeByte(0xC5);
	playermsg.writeByte(0);
	playermsg.writeByte(0x01); // No data available
	writeToOutputBuffer();
}

void ProtocolGame::sendReLoginWindow(uint8_t unfairFightReduction)
{
	playermsg.reset();
	playermsg.writeByte(CanaryLib::GameServerDeath);
	#if GAME_FEATURE_DEATH_TYPE > 0
	playermsg.writeByte(0x00);
	#endif
	#if GAME_FEATURE_DEATH_PENALTY > 0
	playermsg.writeByte(unfairFightReduction);
	#else
	(void)unfairFightReduction;
	#endif
	playermsg.writeByte(0x01); // use death redemption (boolean)
	writeToOutputBuffer();
}

void ProtocolGame::sendStats()
{
	playermsg.reset();
	AddPlayerStats();
	writeToOutputBuffer();
}

void ProtocolGame::sendBasicData()
{
	playermsg.reset();
	playermsg.writeByte(0x9F);
	if (player->isPremium()) {
		playermsg.writeByte(1);
		#if GAME_FEATURE_PREMIUM_EXPIRATION > 0
		playermsg.write<uint32_t>(time(nullptr) + (player->premiumDays * 86400));
		#endif
	} else {
		playermsg.writeByte(0);
		#if GAME_FEATURE_PREMIUM_EXPIRATION > 0
		playermsg.write<uint32_t>(0);
		#endif
	}
	playermsg.writeByte(player->getVocation()->getClientId());

	std::vector<uint16_t> spells = g_spells().getSpellsByVocation(player->getVocationId());
	playermsg.write<uint16_t>(spells.size());
	for (auto spellId : spells) {
		playermsg.writeByte(spellId);
	}
	writeToOutputBuffer();
}

/*void ProtocolGame::sendBlessStatus()
{
	uint16_t haveBlesses = 0;
	uint8_t blessings = (8);
	for (uint8_t i = 1; i < blessings; i++) {
		if (player->hasBlessing(i)) {
			haveBlesses++;
		}
	}

	playermsg.reset();
	playermsg.writeByte(0x9C);
	if (haveBlesses >= 5) {
		playermsg.write<uint16_t>((static_cast<uint16_t>(1) << haveBlesses) - 1);
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	playermsg.writeByte((haveBlesses >= 5 ? 2 : 1));
	writeToOutputBuffer();
}*/

void ProtocolGame::sendTextMessage(const TextMessage& message)
{
	uint8_t messageType = translateMessageClassToClient(message.type);
	playermsg.reset();
	playermsg.writeByte(0xB4);
	playermsg.writeByte(messageType);
	switch (message.type) {
		case MESSAGE_DAMAGE_DEALT:
		case MESSAGE_DAMAGE_RECEIVED:
		case MESSAGE_DAMAGE_OTHERS: {
			playermsg.addPosition(message.position);
			playermsg.write<uint32_t>(message.primary.value);
			playermsg.writeByte(message.primary.color);
			playermsg.write<uint32_t>(message.secondary.value);
			playermsg.writeByte(message.secondary.color);
			break;
		}
		case MESSAGE_MANA:
		case MESSAGE_HEALED:
		case MESSAGE_HEALED_OTHERS:
		case MESSAGE_EXPERIENCE:
		case MESSAGE_EXPERIENCE_OTHERS: {
			playermsg.addPosition(message.position);
			playermsg.write<uint32_t>(message.primary.value);
			playermsg.writeByte(message.primary.color);
			break;
		}
		case MESSAGE_GUILD:
		case MESSAGE_PARTY_MANAGEMENT:
		case MESSAGE_PARTY:
			playermsg.write<uint16_t>(message.channelId);
			break;
		default: {
			break;
		}
	}
	playermsg.writeString(message.text);
	writeToOutputBuffer();
}

void ProtocolGame::sendClosePrivate(uint16_t channelId)
{
	playermsg.reset();
	playermsg.writeByte(0xB3);
	playermsg.write<uint16_t>(channelId);
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName)
{
	playermsg.reset();
	playermsg.writeByte(0xB2);
	playermsg.write<uint16_t>(channelId);
	playermsg.writeString(channelName);
	#if GAME_FEATURE_CHAT_PLAYERLIST > 0
	playermsg.write<uint16_t>(0x01);
	playermsg.writeString(player->getName());
	playermsg.write<uint16_t>(0x00);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendChannelsDialog()
{
	playermsg.reset();
	playermsg.writeByte(0xAB);

	const ChannelList& list = g_chat().getChannelList(*player);
	playermsg.writeByte(list.size());
	for (ChatChannel* channel : list) {
		playermsg.write<uint16_t>(channel->getId());
		playermsg.writeString(channel->getName());
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendChannel(uint16_t channelId, const std::string& channelName, const UsersMap* channelUsers, const InvitedMap* invitedUsers)
{
	playermsg.reset();
	playermsg.writeByte(0xAC);
	playermsg.write<uint16_t>(channelId);
	playermsg.writeString(channelName);
	#if GAME_FEATURE_CHAT_PLAYERLIST > 0
	if (channelUsers) {
		playermsg.write<uint16_t>(channelUsers->size());
		for (const auto& it : *channelUsers) {
			playermsg.writeString(it.second->getName());
		}
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (invitedUsers) {
		playermsg.write<uint16_t>(invitedUsers->size());
		for (const auto& it : *invitedUsers) {
			playermsg.writeString(it.second->getName());
		}
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	#else
	(void)channelUsers;
	(void)invitedUsers;
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel)
{
	uint8_t talkType = translateSpeakClassToClient(type);
	if (talkType == TALKTYPE_NONE) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0xAA);
	#if GAME_FEATURE_MESSAGE_STATEMENT > 0
	playermsg.write<uint32_t>(0x00);
	#endif
	playermsg.writeString(author);
	#if GAME_FEATURE_MESSAGE_LEVEL > 0
	playermsg.write<uint16_t>(0x00);
	#endif
	playermsg.writeByte(talkType);
	playermsg.write<uint16_t>(channel);
	playermsg.writeString(text);
	writeToOutputBuffer();
}

void ProtocolGame::sendIcons(uint32_t icons)
{
	playermsg.reset();
	playermsg.writeByte(0xA2);
	#if GAME_FEATURE_PLAYERICONS_U32 > 0
	playermsg.write<uint32_t>(icons);
	#elif GAME_FEATURE_PLAYERICONS_U16 > 0
	playermsg.write<uint16_t>(static_cast<uint16_t>(icons));
	#else
	playermsg.writeByte(static_cast<uint8_t>(icons));
	#endif
	writeToOutputBuffer();
}

#if GAME_FEATURE_CONTAINER_PAGINATION > 0
void ProtocolGame::sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex)
#else
void ProtocolGame::sendContainer(uint8_t cid, const Container* container, bool hasParent)
#endif
{
	playermsg.reset();
	playermsg.writeByte(0x6E);

	playermsg.writeByte(cid);
	#if GAME_FEATURE_BROWSEFIELD > 0
	if (container->getID() == ITEM_BROWSEFIELD) {
		AddItem(ITEM_BAG, 1);
		playermsg.writeString("Browse Field");
	} else {
		AddItem(container);

		const std::string& containerName = container->getName();
		playermsg.writeString((containerName.empty() ? (std::string("item of type ") + std::to_string(container->getID())) : containerName));
	}
	#else
	AddItem(container);
	playermsg.writeString(container->getName());
	#endif

	playermsg.writeByte(container->capacity());

	playermsg.writeByte(hasParent ? 0x01 : 0x00);
	//can use depot search
	// playermsg.writeByte(0x00);

	#if GAME_FEATURE_CONTAINER_PAGINATION > 0
	playermsg.writeByte(container->isUnlocked() ? 0x01 : 0x00); // Drag and drop
	playermsg.writeByte(container->hasPagination() ? 0x01 : 0x00); // Pagination

	uint32_t containerSize = container->size();
	playermsg.write<uint16_t>(containerSize);
	playermsg.write<uint16_t>(firstIndex);
	if (firstIndex < containerSize) {
		uint8_t itemsToSend = std::min<uint32_t>(std::min<uint32_t>(container->capacity(), containerSize - firstIndex), std::numeric_limits<uint8_t>::max());

		playermsg.writeByte(itemsToSend);
		for (auto it = container->getItemList().begin() + firstIndex, end = it + itemsToSend; it != end; ++it) {
			AddItem(*it);
		}
	} else {
		playermsg.writeByte(0x00);
	}
	#else
	uint8_t itemsToSend = std::min<uint32_t>(container->size(), std::numeric_limits<uint8_t>::max());

	playermsg.writeByte(itemsToSend);
	for (auto it = container->getItemList().begin(), end = it + itemsToSend; it != end; ++it) {
		AddItem(*it);
	}
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendShop(Npc* npc, const ShopInfoList& itemList)
{
	playermsg.reset();
	playermsg.writeByte(0x7A);
	#if GAME_FEATURE_NPC_NAME_ON_TRADE > 0
	playermsg.writeString(npc->getName());
	#else
	(void)npc;
	#endif
	// TODO: enhance OTC to have this extra byte
	// playermsg.addItemId(ITEM_GOLD_COIN);
	// playermsg.writeString(std::string());

	uint16_t itemsToSend = std::min<size_t>(itemList.size(), std::numeric_limits<uint16_t>::max());
	playermsg.write<uint16_t>(itemsToSend);

	uint16_t i = 0;
	for (auto it = itemList.begin(); i < itemsToSend; ++it, ++i) {
		AddShopItem(*it);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendCloseShop()
{
	playermsg.reset();
	playermsg.writeByte(0x7C);
	writeToOutputBuffer();
}

void ProtocolGame::sendSaleItemList(const std::vector<ShopInfo>& shop, const std::map<uint32_t, uint32_t>& inventoryMap)
{
	//Since we already have full inventory map we shouldn't call getMoney here - it is simply wasting cpu power
	uint64_t playerMoney = 0;
	auto it = inventoryMap.find(ITEM_CRYSTAL_COIN);
	if (it != inventoryMap.end()) {
		playerMoney += static_cast<uint64_t>(it->second) * 10000;
	}
	it = inventoryMap.find(ITEM_PLATINUM_COIN);
	if (it != inventoryMap.end()) {
		playerMoney += static_cast<uint64_t>(it->second) * 100;
	}
	it = inventoryMap.find(ITEM_GOLD_COIN);
	if (it != inventoryMap.end()) {
		playerMoney += static_cast<uint64_t>(it->second);
	}

	playermsg.reset();
	playermsg.writeByte(0xEE);
	playermsg.writeByte(0x00);
	playermsg.write<uint64_t>(player->getBankBalance());
	writeToOutputBuffer();

	playermsg.reset();
	playermsg.writeByte(0xEE);
	playermsg.writeByte(0x01);
	playermsg.write<uint64_t>(playerMoney);
	writeToOutputBuffer();

	playermsg.reset();
	playermsg.writeByte(0x7B);
	playermsg.write<uint64_t>(playerMoney);

	uint8_t itemsToSend = 0;
	auto msgPosition = playermsg.getBufferPosition();
	playermsg.skip(1);

	for (const ShopInfo& shopInfo : shop) {
		if (shopInfo.sellPrice == 0) {
			continue;
		}

		uint32_t index = static_cast<uint32_t>(shopInfo.itemId);
		if (Item::items[shopInfo.itemId].isFluidContainer()) {
			index |= (static_cast<uint32_t>(shopInfo.subType) << 16);
		}

		it = inventoryMap.find(index);
		if (it != inventoryMap.end()) {
			playermsg.addItemId(shopInfo.itemId);
			playermsg.writeByte(std::min<uint32_t>(it->second, std::numeric_limits<uint8_t>::max()));
			if (++itemsToSend >= 0xFF) {
				break;
			}
		}
	}

	playermsg.setBufferPosition(msgPosition);
	playermsg.writeByte(itemsToSend);
	writeToOutputBuffer();
}

#if GAME_FEATURE_MARKET > 0
void ProtocolGame::sendMarketEnter(uint32_t depotId)
{
	playermsg.reset();
	playermsg.writeByte(0xF6);

	playermsg.write<uint64_t>(player->getBankBalance());
	playermsg.writeByte(std::min<uint32_t>(IOMarket::getPlayerOfferCount(player->getGUID()), std::numeric_limits<uint8_t>::max()));

	DepotChest* depotChest = player->getDepotChest(depotId, false);
	if (!depotChest) {
		playermsg.write<uint16_t>(0x00);
		writeToOutputBuffer();
		return;
	}

	player->setInMarket(true);

	std::map<uint16_t, uint32_t> depotItems;
	std::vector<Container*> containers{ depotChest, player->getInbox() };

  size_t ic = 0;
	do {
		Container* container = containers[ic++];

		for (Item* item : container->getItemList()) {
			Container* c = item->getContainer();
			if (c && !c->empty()) {
				containers.push_back(c);
				continue;
			}

			const ItemType& itemType = Item::items[item->getID()];
			if (itemType.wareId == 0) {
				continue;
			}

			if (c && (!itemType.isContainer() || c->capacity() != itemType.maxItems)) {
				continue;
			}

			if (!item->hasMarketAttributes()) {
				continue;
			}

			depotItems[itemType.wareId] += Item::countByType(item, -1);
		}
	} while (ic < containers.size());

	uint16_t itemsToSend = std::min<size_t>(depotItems.size(), std::numeric_limits<uint16_t>::max());
	playermsg.write<uint16_t>(itemsToSend);

	uint16_t i = 0;
	for (std::map<uint16_t, uint32_t>::const_iterator it = depotItems.begin(); i < itemsToSend; ++it, ++i) {
		playermsg.write<uint16_t>(it->first);
		playermsg.write<uint16_t>(std::min<uint32_t>(0xFFFF, it->second));
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketLeave()
{
	playermsg.reset();
	playermsg.writeByte(0xF7);
	writeToOutputBuffer();
}

void ProtocolGame::sendMarketBrowseItem(uint16_t itemId, const MarketOfferList& buyOffers, const MarketOfferList& sellOffers)
{
	playermsg.reset();
	playermsg.writeByte(0xF9);
	playermsg.addItemId(itemId);

	playermsg.write<uint32_t>(buyOffers.size());
	for (const MarketOffer& offer : buyOffers) {
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
		playermsg.writeString(offer.playerName);
	}

	playermsg.write<uint32_t>(sellOffers.size());
	for (const MarketOffer& offer : sellOffers) {
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
		playermsg.writeString(offer.playerName);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketAcceptOffer(const MarketOfferEx& offer)
{
	playermsg.reset();
	playermsg.writeByte(0xF9);
	playermsg.addItemId(offer.itemId);
	if (offer.type == MARKETACTION_BUY) {
		playermsg.write<uint32_t>(0x01);
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
		playermsg.writeString(offer.playerName);
		playermsg.write<uint32_t>(0x00);
	} else {
		playermsg.write<uint32_t>(0x00);
		playermsg.write<uint32_t>(0x01);
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
		playermsg.writeString(offer.playerName);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketBrowseOwnOffers(const MarketOfferList& buyOffers, const MarketOfferList& sellOffers)
{
	playermsg.reset();
	playermsg.writeByte(0xF9);
	playermsg.write<uint16_t>(MARKETREQUEST_OWN_OFFERS);

	playermsg.write<uint32_t>(buyOffers.size());
	for (const MarketOffer& offer : buyOffers) {
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.addItemId(offer.itemId);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
	}

	playermsg.write<uint32_t>(sellOffers.size());
	for (const MarketOffer& offer : sellOffers) {
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.addItemId(offer.itemId);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketCancelOffer(const MarketOfferEx& offer)
{
	playermsg.reset();
	playermsg.writeByte(0xF9);
	playermsg.write<uint16_t>(MARKETREQUEST_OWN_OFFERS);
	if (offer.type == MARKETACTION_BUY) {
		playermsg.write<uint32_t>(0x01);
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.addItemId(offer.itemId);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
		playermsg.write<uint32_t>(0x00);
	} else {
		playermsg.write<uint32_t>(0x00);
		playermsg.write<uint32_t>(0x01);
		playermsg.write<uint32_t>(offer.timestamp);
		playermsg.write<uint16_t>(offer.counter);
		playermsg.addItemId(offer.itemId);
		playermsg.write<uint16_t>(offer.amount);
		playermsg.write<uint32_t>(offer.price);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketBrowseOwnHistory(const HistoryMarketOfferList& buyOffers, const HistoryMarketOfferList& sellOffers)
{
	uint32_t i = 0;
	std::map<uint32_t, uint16_t> counterMap;
	uint32_t buyOffersToSend = std::min<uint32_t>(buyOffers.size(), 810 + std::max<int32_t>(0, 810 - sellOffers.size()));
	uint32_t sellOffersToSend = std::min<uint32_t>(sellOffers.size(), 810 + std::max<int32_t>(0, 810 - buyOffers.size()));

	playermsg.reset();
	playermsg.writeByte(0xF9);
	playermsg.write<uint16_t>(MARKETREQUEST_OWN_HISTORY);

	playermsg.write<uint32_t>(buyOffersToSend);
	for (auto it = buyOffers.begin(); i < buyOffersToSend; ++it, ++i) {
		playermsg.write<uint32_t>(it->timestamp);
		playermsg.write<uint16_t>(counterMap[it->timestamp]++);
		playermsg.addItemId(it->itemId);
		playermsg.write<uint16_t>(it->amount);
		playermsg.write<uint32_t>(it->price);
		playermsg.writeByte(it->state);
	}

	counterMap.clear();
	i = 0;

	playermsg.write<uint32_t>(sellOffersToSend);
	for (auto it = sellOffers.begin(); i < sellOffersToSend; ++it, ++i) {
		playermsg.write<uint32_t>(it->timestamp);
		playermsg.write<uint16_t>(counterMap[it->timestamp]++);
		playermsg.addItemId(it->itemId);
		playermsg.write<uint16_t>(it->amount);
		playermsg.write<uint32_t>(it->price);
		playermsg.writeByte(it->state);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendMarketDetail(uint16_t itemId)
{
	playermsg.reset();
	playermsg.writeByte(0xF8);
	playermsg.addItemId(itemId);

	const ItemType& it = Item::items[itemId];
	if (it.armor != 0) {
		playermsg.writeString(std::to_string(it.armor));
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.attack != 0) {
		// TODO: chance to hit, range
		// example:
		// "attack +x, chance to hit +y%, z fields"
		if (it.abilities && it.abilities->elementType != COMBAT_NONE && it.abilities->elementDamage != 0) {
			std::ostringstream ss;
			ss << it.attack << " physical +" << it.abilities->elementDamage << ' ' << getCombatName(it.abilities->elementType);
			playermsg.writeString(ss.str());
		} else {
			playermsg.writeString(std::to_string(it.attack));
		}
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.isContainer()) {
		playermsg.writeString(std::to_string(it.maxItems));
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.defense != 0) {
		if (it.extraDefense != 0) {
			std::ostringstream ss;
			ss << it.defense << ' ' << std::showpos << it.extraDefense << std::noshowpos;
			playermsg.writeString(ss.str());
		} else {
			playermsg.writeString(std::to_string(it.defense));
		}
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (!it.description.empty()) {
		const std::string& descr = it.description;
		if (descr.back() == '.') {
			playermsg.writeString(std::string(descr, 0, descr.length() - 1));
		} else {
			playermsg.writeString(descr);
		}
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.decayTime != 0) {
		std::ostringstream ss;
		ss << it.decayTime << " seconds";
		playermsg.writeString(ss.str());
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;
		for (size_t i = 0; i < COMBAT_COUNT; ++i) {
			if (it.abilities->absorbPercent[i] == 0) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << getCombatName(indexToCombatType(i)) << ' ' << std::showpos << it.abilities->absorbPercent[i] << std::noshowpos << '%';
		}

		playermsg.writeString(ss.str());
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.minReqLevel != 0) {
		playermsg.writeString(std::to_string(it.minReqLevel));
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.minReqMagicLevel != 0) {
		playermsg.writeString(std::to_string(it.minReqMagicLevel));
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	playermsg.writeString(it.vocationString);

	playermsg.writeString(it.runeSpellName);
	if (it.abilities) {
		std::ostringstream ss;
		bool separator = false;
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; i++) {
			if (!it.abilities->skills[i]) {
				continue;
			}

			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << getSkillName(i) << ' ' << std::showpos << it.abilities->skills[i] << std::noshowpos;
		}

		if (it.abilities->stats[STAT_MAGICPOINTS] != 0) {
			if (separator) {
				ss << ", ";
			} else {
				separator = true;
			}

			ss << "magic level " << std::showpos << it.abilities->stats[STAT_MAGICPOINTS] << std::noshowpos;
		}

		if (it.abilities->speed != 0) {
			if (separator) {
				ss << ", ";
			}

			ss << "speed " << std::showpos << (it.abilities->speed >> 1) << std::noshowpos;
		}

		playermsg.writeString(ss.str());
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	if (it.charges != 0) {
		playermsg.writeString(std::to_string(it.charges));
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	std::string weaponName = getWeaponName(it.weaponType);
	if (it.slotPosition & SLOTP_TWO_HAND) {
		if (!weaponName.empty()) {
			weaponName += ", two-handed";
		} else {
			weaponName = "two-handed";
		}
	}

	playermsg.writeString(weaponName);
	if (it.weight != 0) {
		std::ostringstream ss;
		if (it.weight < 10) {
			ss << "0.0" << it.weight;
		} else if (it.weight < 100) {
			ss << "0." << it.weight;
		} else {
			std::string weightString = std::to_string(it.weight);
			weightString.insert(weightString.end() - 2, '.');
			ss << weightString;
		}
		ss << " oz";
		playermsg.writeString(ss.str());
	} else {
		playermsg.write<uint16_t>(0x00);
	}

	// TODO: double check this 2bytes in OTC makertprotocol.lua
	playermsg.write<uint16_t>(0x00);

	MarketStatistics* statistics = IOMarket::getInstance().getPurchaseStatistics(itemId);
	if (statistics) {
		playermsg.writeByte(0x01);
		playermsg.write<uint32_t>(statistics->numTransactions);
		playermsg.write<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), statistics->totalPrice));
		playermsg.write<uint32_t>(statistics->highestPrice);
		playermsg.write<uint32_t>(statistics->lowestPrice);
	} else {
		playermsg.writeByte(0x00);
	}

	statistics = IOMarket::getInstance().getSaleStatistics(itemId);
	if (statistics) {
		playermsg.writeByte(0x01);
		playermsg.write<uint32_t>(statistics->numTransactions);
		playermsg.write<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), statistics->totalPrice));
		playermsg.write<uint32_t>(statistics->highestPrice);
		playermsg.write<uint32_t>(statistics->lowestPrice);
	} else {
		playermsg.writeByte(0x00);
	}

	writeToOutputBuffer();
}
#endif

void ProtocolGame::sendQuestLog()
{
	playermsg.reset();
	playermsg.writeByte(0xF0);
	playermsg.write<uint16_t>(g_game().quests.getQuestsCount(player));
	for (const Quest& quest : g_game().quests.getQuests()) {
		if (quest.isStarted(player)) {
			playermsg.write<uint16_t>(quest.getID());
			playermsg.writeString(quest.getName());
			playermsg.writeByte(quest.isCompleted(player));
		}
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendQuestLine(const Quest* quest)
{
	playermsg.reset();
	playermsg.writeByte(0xF1);
	playermsg.write<uint16_t>(quest->getID());
	playermsg.writeByte(quest->getMissionsCount(player));
	for (const Mission& mission : quest->getMissions()) {
		if (mission.isStarted(player)) {
			#if GAME_FEATURE_QUEST_TRACKER > 0
			playermsg.write<uint16_t>(mission.getMissionId());
			#endif
			playermsg.writeString(mission.getName(player));
			playermsg.writeString(mission.getDescription(player));
		}
	}
	writeToOutputBuffer();
}

#if GAME_FEATURE_QUEST_TRACKER > 0
void ProtocolGame::sendTrackedQuests(uint8_t remainingQuests, std::vector<uint16_t>& quests)
{
	playermsg.reset();
	playermsg.writeByte(0xD0);
	playermsg.writeByte(0x01);
	playermsg.writeByte(remainingQuests);
	playermsg.writeByte(static_cast<uint8_t>(quests.size()));
	for (uint16_t missionId : quests) {
		const Mission* mission = g_game().quests.getMissionByID(missionId);
		if (mission) {
			Quest* quest = g_game().quests.getQuestByID(mission->getQuestId());
			playermsg.write<uint16_t>(missionId);
			playermsg.writeString((quest ? quest->getName() : std::string()));
			playermsg.writeString(mission->getName(player));
			playermsg.writeString(mission->getDescription(player));
		} else {
			playermsg.write<uint16_t>(missionId);
			playermsg.writeString("Unknown Error");
			playermsg.writeString("Unknown Error");
			playermsg.writeString("Unknown Error");
		}
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendUpdateTrackedQuest(const Mission* mission)
{
	playermsg.reset();
	playermsg.writeByte(0xD0);
	playermsg.writeByte(0x00);
	playermsg.write<uint16_t>(mission->getMissionId());
	playermsg.writeString(mission->getName(player));
	playermsg.writeString(mission->getDescription(player));
	writeToOutputBuffer();
}
#endif

void ProtocolGame::sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack)
{
	playermsg.reset();
	if (ack) {
		playermsg.writeByte(0x7D);
	} else {
		playermsg.writeByte(0x7E);
	}

	playermsg.writeString(traderName);
	if (const Container* tradeContainer = item->getContainer()) {
		std::list<const Container*> listContainer {tradeContainer};
		std::list<const Item*> itemList {tradeContainer};
		while (!listContainer.empty()) {
			const Container* container = listContainer.front();
			listContainer.pop_front();
			for (Item* containerItem : container->getItemList()) {
				Container* tmpContainer = containerItem->getContainer();
				if (tmpContainer) {
					listContainer.push_back(tmpContainer);
				}
				itemList.push_back(containerItem);
			}
		}

		playermsg.writeByte(itemList.size());
		for (const Item* listItem : itemList) {
			AddItem(listItem);
		}
	} else {
		playermsg.writeByte(0x01);
		AddItem(item);
	}
	writeToOutputBuffer();
}

void ProtocolGame::sendCloseTrade()
{
	playermsg.reset();
	playermsg.writeByte(0x7F);
	writeToOutputBuffer();
}

void ProtocolGame::sendCloseContainer(uint8_t cid)
{
	playermsg.reset();
	playermsg.writeByte(0x6F);
	playermsg.writeByte(cid);
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureTurn(const Creature* creature, uint32_t stackPos)
{
	if (!canSee(creature)) {
		return;
	}
	
	playermsg.reset();
	playermsg.writeByte(0x6B);
	playermsg.addPosition(creature->getPosition());
	playermsg.writeByte(stackPos);
	playermsg.write<uint16_t>(0x63);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte(creature->getDirection());
	playermsg.writeByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
	writeToOutputBuffer();
}

void ProtocolGame::sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos/* = nullptr*/)
{
	uint8_t talkType = translateSpeakClassToClient(type);
	if (talkType == TALKTYPE_NONE) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0xAA);
	#if GAME_FEATURE_MESSAGE_STATEMENT > 0
	static uint32_t statementId = 0;
	playermsg.write<uint32_t>(++statementId);
	#endif
	playermsg.writeString(creature->getName());

	//Add level only for players
	#if GAME_FEATURE_MESSAGE_LEVEL > 0
	if (const Player* speaker = creature->getPlayer()) {
		playermsg.write<uint16_t>(speaker->getLevel());
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	#endif

	playermsg.writeByte(talkType);
	if (pos) {
		playermsg.addPosition(*pos);
	} else {
		playermsg.addPosition(creature->getPosition());
	}

	playermsg.writeString(text);
	writeToOutputBuffer();
}

void ProtocolGame::sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId)
{
	uint8_t talkType = translateSpeakClassToClient(type);
	if (talkType == TALKTYPE_NONE) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0xAA);
	#if GAME_FEATURE_MESSAGE_STATEMENT > 0
	static uint32_t statementId = 0;
	playermsg.write<uint32_t>(++statementId);
	#endif
	if (!creature) {
		playermsg.write<uint16_t>(0x00);
		#if GAME_FEATURE_MESSAGE_LEVEL > 0
		playermsg.write<uint16_t>(0x00);
		#endif
	} else if (type == TALKTYPE_CHANNEL_R2) {
		playermsg.write<uint16_t>(0x00);
		#if GAME_FEATURE_MESSAGE_LEVEL > 0
		playermsg.write<uint16_t>(0x00);
		#endif
		type = TALKTYPE_CHANNEL_R1;
	} else {
		playermsg.writeString(creature->getName());

		//Add level only for players
		#if GAME_FEATURE_MESSAGE_LEVEL > 0
		if (const Player* speaker = creature->getPlayer()) {
			playermsg.write<uint16_t>(speaker->getLevel());
		} else {
			playermsg.write<uint16_t>(0x00);
		}
		#endif
	}

	playermsg.writeByte(talkType);
	playermsg.write<uint16_t>(channelId);
	playermsg.writeString(text);
	writeToOutputBuffer();
}

void ProtocolGame::sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text)
{
	uint8_t talkType = translateSpeakClassToClient(type);
	if (talkType == TALKTYPE_NONE) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0xAA);
	#if GAME_FEATURE_MESSAGE_STATEMENT > 0
	static uint32_t statementId = 0;
	playermsg.write<uint32_t>(++statementId);
	#endif
	if (speaker) {
		playermsg.writeString(speaker->getName());
		#if GAME_FEATURE_MESSAGE_LEVEL > 0
		playermsg.write<uint16_t>(speaker->getLevel());
		#endif
	} else {
		playermsg.write<uint16_t>(0x00);
		#if GAME_FEATURE_MESSAGE_LEVEL > 0
		playermsg.write<uint16_t>(0x00);
		#endif
	}
	playermsg.writeByte(talkType);
	playermsg.writeString(text);
	writeToOutputBuffer();
}

void ProtocolGame::sendCancelTarget()
{
	playermsg.reset();
	playermsg.writeByte(0xA3);
	#if GAME_FEATURE_ATTACK_SEQUENCE > 0
	playermsg.write<uint32_t>(0x00);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendChangeSpeed(const Creature* creature, uint32_t speed)
{
	playermsg.reset();
	playermsg.writeByte(0x8F);
	playermsg.write<uint32_t>(creature->getID());
	#if GAME_FEATURE_NEWSPEED_LAW > 0
	playermsg.write<uint16_t>(creature->getBaseSpeed() / 2);
	playermsg.write<uint16_t>(speed / 2);
	#else
	playermsg.write<uint16_t>(creature->getBaseSpeed());
	playermsg.write<uint16_t>(speed);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendCancelWalk()
{
	playermsg.reset();
	playermsg.writeByte(0xB5);
	playermsg.writeByte(player->getDirection());
	writeToOutputBuffer();
}

void ProtocolGame::sendSkills()
{
	playermsg.reset();
	AddPlayerSkills();
	writeToOutputBuffer();
}

void ProtocolGame::sendPing()
{
	playermsg.reset();
	#if GAME_FEATURE_PING > 0
	playermsg.writeByte(CanaryLib::GameServerPingBack);
	#else
	playermsg.writeByte(CanaryLib::GameServerPing);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendPingBack()
{
	playermsg.reset();
	playermsg.writeByte(CanaryLib::GameServerPing);
	writeToOutputBuffer();
}

void ProtocolGame::sendDistanceShoot(const Position& from, const Position& to, uint8_t type)
{
	// TODO: improve OTC to parse new effect model
	// #if CLIENT_VERSION >= 1203
	// playermsg.reset();
	// playermsg.writeByte(0x83);
	// playermsg.addPosition(from);
	// playermsg.writeByte(MAGIC_EFFECTS_CREATE_DISTANCEEFFECT);
	// playermsg.writeByte(type);
	// playermsg.writeByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int16_t>(to.x - from.x))));
	// playermsg.writeByte(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int16_t>(to.y - from.y))));
	// playermsg.writeByte(MAGIC_EFFECTS_END_LOOP);
	// writeToOutputBuffer();
	// #else
	playermsg.reset();
	playermsg.writeByte(0x85);
	playermsg.addPosition(from);
	playermsg.addPosition(to);
	playermsg.writeByte(type);
	writeToOutputBuffer();
	// #endif
}

void ProtocolGame::sendMagicEffect(const Position& pos, uint8_t type)
{
	if (!canSee(pos)) {
		return;
	}

	// TODO: improve OTC to parse new effect model
	// #if CLIENT_VERSION >= 1203
	// playermsg.reset();
	// playermsg.writeByte(0x83);
	// playermsg.addPosition(pos);
	// playermsg.writeByte(MAGIC_EFFECTS_CREATE_EFFECT);
	// playermsg.writeByte(type);
	// playermsg.writeByte(MAGIC_EFFECTS_END_LOOP);
	// writeToOutputBuffer();
	// #else
	playermsg.reset();
	playermsg.writeByte(0x83);
	playermsg.addPosition(pos);
	playermsg.writeByte(type);
	writeToOutputBuffer();
	// #endif
}

void ProtocolGame::sendCreatureHealth(const Creature* creature, uint8_t healthPercent)
{
	playermsg.reset();
	playermsg.writeByte(0x8C);
	playermsg.write<uint32_t>(creature->getID());
	if (creature->isHealthHidden() && creature != player) {
		playermsg.writeByte(0x00);
	} else {
		playermsg.writeByte(healthPercent);
	}
	writeToOutputBuffer();
}

#if GAME_FEATURE_PARTY_LIST > 0
void ProtocolGame::sendPartyCreatureUpdate(const Creature* target)
{
	bool known;
	uint32_t removedKnown;
	uint32_t cid = target->getID();
	checkCreatureAsKnown(cid, known, removedKnown);

	playermsg.reset();
	playermsg.writeByte(0x8B);
	playermsg.write<uint32_t>(cid);
	playermsg.writeByte(0);//creature update
	AddCreature(player, known, removedKnown);
	writeToOutputBuffer();
}

void ProtocolGame::sendPartyCreatureShield(const Creature* target)
{
	uint32_t cid = target->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		sendPartyCreatureUpdate(target);
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x91);
	playermsg.write<uint32_t>(target->getID());
	playermsg.writeByte(player->getPartyShield(target->getPlayer()));
	writeToOutputBuffer();
}

void ProtocolGame::sendPartyCreatureSkull(const Creature* target)
{
	if (g_game().getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	uint32_t cid = target->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		sendPartyCreatureUpdate(target);
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x90);
	playermsg.write<uint32_t>(target->getID());
	playermsg.writeByte(player->getSkullClient(target));
	writeToOutputBuffer();
}

void ProtocolGame::sendPartyCreatureHealth(const Creature* target, uint8_t healthPercent)
{
	uint32_t cid = target->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		sendPartyCreatureUpdate(target);
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x8C);
	playermsg.write<uint32_t>(cid);
	playermsg.writeByte(healthPercent);
	writeToOutputBuffer();
}

void ProtocolGame::sendPartyPlayerMana(const Player* target, uint8_t manaPercent)
{
	uint32_t cid = target->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		sendPartyCreatureUpdate(target);
	}

	playermsg.reset();
	playermsg.writeByte(0x8B);
	playermsg.write<uint32_t>(cid);
	playermsg.writeByte(11);//mana percent
	playermsg.writeByte(manaPercent);
	writeToOutputBuffer();
}

void ProtocolGame::sendPartyCreatureShowStatus(const Creature* target, bool showStatus)
{
	uint32_t cid = target->getID();
	if (knownCreatureSet.find(cid) == knownCreatureSet.end()) {
		sendPartyCreatureUpdate(target);
	}

	playermsg.reset();
	playermsg.writeByte(0x8B);
	playermsg.write<uint32_t>(cid);
	playermsg.writeByte(12);//show status
	playermsg.writeByte((showStatus ? 0x01 : 0x00));
	writeToOutputBuffer();
}
#endif

void ProtocolGame::sendFYIBox(const std::string& message)
{
	playermsg.reset();
	playermsg.writeByte(CanaryLib::GameServerLoginAdvice);
	playermsg.writeString(message);
	writeToOutputBuffer();
}

//tile
void ProtocolGame::sendMapDescription(const Position& pos)
{
  flatbuffers::FlatBufferBuilder fbb;

  int8_t startz = 7;
  int8_t endz = 0;
  int8_t zstep = -1;

  if (pos.z > 7) {
    startz = pos.z - 2;
    endz = std::min<int8_t>(MAP_MAX_LAYERS - 1, pos.z + 2);
    zstep = 1;
  }

  for (int8_t nz = startz; nz != endz + zstep; nz += zstep) {
    int8_t offset = pos.z - nz;

    std::vector<Tile*> tileVector = g_game().map.getFloorTiles(
      pos.x + offset,
      pos.y + offset,
      CLIENT_MAP_WIDTH,
      CLIENT_MAP_HEIGHT,
      pos.z
    );

    for (Tile* tile : tileVector) {
      if (!tile) continue;

      uint8_t MAX_ITEMS_PER_TILE = 32;
      uint8_t remainingItemSlots = MAX_ITEMS_PER_TILE;
      bool isPlayerTile = tile->getPosition() == player->getPosition();

      if (Item* ground = tile->getGround()) {
        sendItem(ground, tile->getPosition(), true);
        remainingItemSlots--;
      }

      if (const TileItemVector* items = tile->getItemList()) {
        for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
          sendItem(*it, tile->getPosition(), remainingItemSlots == MAX_ITEMS_PER_TILE);
          if (--remainingItemSlots == 0) break;
        }
      }

      if (isPlayerTile) {
        sendCreature(player, tile->getPosition());
      }

      if (const CreatureVector* creatures = tile->getCreatures()) {
        for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
          const Creature* creature = (*it);
          if (creature->getID() == player->getID()) continue;
          sendCreature(creature, tile->getPosition());
        }
      }
    }
  }
}

#if GAME_FEATURE_TILE_ADDTHING_STACKPOS > 0
void ProtocolGame::sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item)
#else
void ProtocolGame::sendAddTileItem(const Position& pos, const Item* item)
#endif
{
	if (!canSee(pos)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x6A);
	playermsg.addPosition(pos);
	#if GAME_FEATURE_TILE_ADDTHING_STACKPOS > 0
	playermsg.writeByte(stackpos);
	#endif
	AddItem(item);
	writeToOutputBuffer();
}

void ProtocolGame::sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (!canSee(pos)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x6B);
	playermsg.addPosition(pos);
	playermsg.writeByte(stackpos);
	AddItem(item);
	writeToOutputBuffer();
}

void ProtocolGame::sendRemoveTileThing(const Position& pos, uint32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	playermsg.reset();
	RemoveTileThing(pos, stackpos);
	writeToOutputBuffer();
}

void ProtocolGame::sendUpdateTile(const Tile* tile, const Position& pos)
{
	if (!canSee(pos)) {
		return;
	}

	playermsg.reset();
	playermsg.writeByte(0x69);
	playermsg.addPosition(pos);
	if (tile) {
		GetTileDescription(tile);
		playermsg.writeByte(0x00);
		playermsg.writeByte(0xFF);
	} else {
		playermsg.writeByte(0x01);
		playermsg.writeByte(0xFF);
	}

	writeToOutputBuffer();
}

void ProtocolGame::sendFightModes()
{
	playermsg.reset();
	playermsg.writeByte(0xA7);
	playermsg.writeByte(player->fightMode);
	playermsg.writeByte(player->chaseMode);
	playermsg.writeByte(player->secureMode);
	playermsg.writeByte(PVP_MODE_DOVE);
	writeToOutputBuffer();
}

void ProtocolGame::sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos, bool isLogin)
{
	if (!canSee(pos)) {
		return;
	}

	if (creature != player) {
		if (stackpos != -1) {
			playermsg.reset();
			playermsg.writeByte(0x6A);
			playermsg.addPosition(pos);
			playermsg.writeByte(stackpos);

			bool known;
			uint32_t removedKnown;
			checkCreatureAsKnown(creature->getID(), known, removedKnown);
			AddCreature(creature, known, removedKnown);
			writeToOutputBuffer();
		}

		if (isLogin) {
			sendMagicEffect(pos, CONST_ME_TELEPORT);
		}
		return;
	}

	playermsg.reset();

	#if GAME_FEATURE_LOGIN_PENDING > 0
	playermsg.writeByte(CanaryLib::GameServerLoginSuccess);
	#else
	playermsg.writeByte(CanaryLib::GameServerLoginOrPendingState);
	#endif

	playermsg.write<uint32_t>(player->getID());
	playermsg.write<uint16_t>(SERVER_BEAT_MILISECONDS);

	#if GAME_FEATURE_NEWSPEED_LAW > 0
	playermsg.addDouble(Creature::speedA, 3);
	playermsg.addDouble(Creature::speedB, 3);
	playermsg.addDouble(Creature::speedC, 3);
	#endif

	// can report bugs?
	if (player->getAccountType() >= ACCOUNT_TYPE_TUTOR) {
		playermsg.writeByte(0x01);
	} else {
		playermsg.writeByte(0x00);
	}

	playermsg.writeByte(0x00); // can change pvp framing option
	playermsg.writeByte(0x00); // expert mode button enabled

	#if GAME_FEATURE_STORE > 0
	playermsg.writeString(g_config().getString(ConfigManager::STORE_URL)); // URL (string) to ingame store images
	playermsg.write<uint16_t>(static_cast<uint16_t>(g_config().getNumber(ConfigManager::STORE_COIN_PACKAGES))); // premium coin package size
	#endif

	if (addExivaRestrictions) {
		playermsg.writeByte(0x01); // exiva button enabled
	}

	#if GAME_FEATURE_TOURNAMENTS > 0
	playermsg.writeByte(0x00); // tournament button enabled
	#endif

	#if GAME_FEATURE_LOGIN_PENDING > 0
	// sendPendingStateEntered
	playermsg.writeByte(CanaryLib::GameServerLoginOrPendingState);
	// sendWorldEnter
	playermsg.writeByte(CanaryLib::GameServerEnterGame);
	#endif

	//gameworld settings
	AddWorldLight(g_game().getWorldLightInfo());

	writeToOutputBuffer();

	sendTibiaTime(g_game().getLightHour());
	sendMapDescription(pos);
	if (isLogin) {
		sendMagicEffect(pos, CONST_ME_TELEPORT);
	}

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<slots_t>(i), player->getInventoryItem(static_cast<slots_t>(i)));
	}

	sendStats();
	sendSkills();
	//sendBlessStatus();

	//player light level
	sendCreatureLight(creature);
	sendVIPEntries();

	sendBasicData();
	player->sendIcons();
}

void ProtocolGame::sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos, int32_t oldStackPos, bool teleport)
{
	if (creature == player) {
		if (oldStackPos >= 10) {
			sendMapDescription(newPos);
		} else if (teleport) {
			playermsg.reset();
			RemoveTileThing(oldPos, oldStackPos);
			writeToOutputBuffer();
			sendMapDescription(newPos);
		} else {
			playermsg.reset();
			if (oldPos.z == 7 && newPos.z >= 8) {
				RemoveTileThing(oldPos, oldStackPos);
			} else {
				playermsg.writeByte(0x6D);
				playermsg.addPosition(oldPos);
				playermsg.writeByte(oldStackPos);
				playermsg.addPosition(newPos);
			}

			if (newPos.z > oldPos.z) {
				MoveDownCreature(creature, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(creature, newPos, oldPos);
			}

			if (oldPos.y > newPos.y) { // north, for old x
				playermsg.writeByte(0x65);
				GetMapDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), newPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z, CLIENT_MAP_WIDTH, 1);
			} else if (oldPos.y < newPos.y) { // south, for old x
				playermsg.writeByte(0x67);
				GetMapDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), newPos.y + CLIENT_MAP_HEIGHT_OFFFSET, newPos.z, CLIENT_MAP_WIDTH, 1);
			}

			if (oldPos.x < newPos.x) { // east, [with new y]
				playermsg.writeByte(0x66);
				GetMapDescription(newPos.x + CLIENT_MAP_WIDTH_OFFSET, newPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z, 1, CLIENT_MAP_HEIGHT);
			} else if (oldPos.x > newPos.x) { // west, [with new y]
				playermsg.writeByte(0x68);
				GetMapDescription(newPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), newPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z, 1, CLIENT_MAP_HEIGHT);
			}
			writeToOutputBuffer();
		}
	} else if (canSee(oldPos) && canSee(creature->getPosition())) {
		if (teleport || (oldPos.z == 7 && newPos.z >= 8) || oldStackPos >= 10) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendAddCreature(creature, newPos, newStackPos, false);
		} else {
			playermsg.reset();
			playermsg.writeByte(0x6D);
			playermsg.addPosition(oldPos);
			playermsg.writeByte(oldStackPos);
			playermsg.addPosition(creature->getPosition());
			writeToOutputBuffer();
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileThing(oldPos, oldStackPos);
	} else if (canSee(creature->getPosition())) {
		sendAddCreature(creature, newPos, newStackPos, false);
	}
}

void ProtocolGame::sendInventoryItem(slots_t slot, const Item* item)
{
	playermsg.reset();
	if (item) {
		playermsg.writeByte(0x78);
		playermsg.writeByte(slot);
		AddItem(item);
	} else {
		playermsg.writeByte(0x79);
		playermsg.writeByte(slot);
	}
	writeToOutputBuffer();
}

#if GAME_FEATURE_INVENTORY_LIST > 0
void ProtocolGame::sendItems(const std::map<uint32_t, uint32_t>& inventoryMap)
{
	playermsg.reset();
	playermsg.writeByte(0xF5);

	uint16_t itemsToSend = 11;
	auto msgPosition = playermsg.getBufferPosition();
	playermsg.skip(2);

	for (uint16_t i = 1; i <= 11; i++) {
		playermsg.write<uint16_t>(i);
		playermsg.writeByte(0);
		playermsg.write<uint16_t>(1);
	}

	for (const auto& inventoryInfo : inventoryMap) {
		uint32_t index = inventoryInfo.first;
		uint8_t fluidType = static_cast<uint8_t>(index >> 16);

		playermsg.addItemId(static_cast<uint16_t>(index));
		playermsg.writeByte((fluidType ? serverFluidToClient(fluidType) : 0));
		playermsg.write<uint16_t>(std::min<uint32_t>(inventoryInfo.second, std::numeric_limits<uint16_t>::max()));

		//Limit it to upper networkmessage buffer size incase player have very large inventory
		if (++itemsToSend >= 0x32F0) {
			break;
		}
	}

	playermsg.setBufferPosition(msgPosition);
	playermsg.write<uint16_t>(itemsToSend);
	writeToOutputBuffer();
}
#endif

#if GAME_FEATURE_CONTAINER_PAGINATION > 0
void ProtocolGame::sendAddContainerItem(uint8_t cid, uint16_t slot, const Item* item)
#else
void ProtocolGame::sendAddContainerItem(uint8_t cid, const Item* item)
#endif
{
	playermsg.reset();
	playermsg.writeByte(0x70);
	playermsg.writeByte(cid);
	#if GAME_FEATURE_CONTAINER_PAGINATION > 0
	playermsg.write<uint16_t>(slot);
	#endif
	AddItem(item);
	writeToOutputBuffer();
}

#if GAME_FEATURE_CONTAINER_PAGINATION > 0
void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item)
#else
void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint8_t slot, const Item* item)
#endif
{
	playermsg.reset();
	playermsg.writeByte(0x71);
	playermsg.writeByte(cid);
	#if GAME_FEATURE_CONTAINER_PAGINATION > 0
	playermsg.write<uint16_t>(slot);
	#else
	playermsg.writeByte(slot);
	#endif
	AddItem(item);
	writeToOutputBuffer();
}

#if GAME_FEATURE_CONTAINER_PAGINATION > 0
void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot, const Item* lastItem)
#else
void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint8_t slot)
#endif
{
	playermsg.reset();
	playermsg.writeByte(0x72);
	playermsg.writeByte(cid);
	#if GAME_FEATURE_CONTAINER_PAGINATION > 0
	playermsg.write<uint16_t>(slot);
	if (lastItem) {
		AddItem(lastItem);
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	#else
	playermsg.writeByte(slot);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite)
{
	playermsg.reset();
	playermsg.writeByte(0x96);
	playermsg.write<uint32_t>(windowTextId);
	AddItem(item);
	if (canWrite) {
		playermsg.write<uint16_t>(maxlen);
		playermsg.writeString(item->getText());
	} else {
		const std::string& text = item->getText();
		playermsg.write<uint16_t>(text.size());
		playermsg.writeString(text);
	}

	const std::string& writer = item->getWriter();
	if (!writer.empty()) {
		playermsg.writeString(writer);
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	
	#if GAME_FEATURE_WRITABLE_DATE > 0
	time_t writtenDate = item->getDate();
	if (writtenDate != 0) {
		playermsg.writeString(formatDateShort(writtenDate));
	} else {
		playermsg.write<uint16_t>(0x00);
	}
	#endif

	writeToOutputBuffer();
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string& text)
{
	playermsg.reset();
	playermsg.writeByte(0x96);
	playermsg.write<uint32_t>(windowTextId);
	AddItem(itemId, 1);
	playermsg.write<uint16_t>(text.size());
	playermsg.writeString(text);
	playermsg.write<uint16_t>(0x00);
	#if GAME_FEATURE_WRITABLE_DATE > 0
	playermsg.write<uint16_t>(0x00);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, const std::string& text)
{
	playermsg.reset();
	playermsg.writeByte(0x97);
	playermsg.writeByte(0x00);
	playermsg.write<uint32_t>(windowTextId);
	playermsg.writeString(text);
	writeToOutputBuffer();
}

void ProtocolGame::sendOutfitWindow()
{
	playermsg.reset();
	playermsg.writeByte(0xC8);

	Outfit_t currentOutfit = player->getDefaultOutfit();
	#if GAME_FEATURE_MOUNTS > 0
	bool mounted = false;
	Mount* currentMount = g_game().mounts.getMountByID(player->getCurrentMount());
	if (currentMount) {
		mounted = (currentOutfit.lookMount == currentMount->clientId);
		currentOutfit.lookMount = currentMount->clientId;
	}
	#endif

	AddOutfit(currentOutfit);

	std::vector<ProtocolOutfit> protocolOutfits;
	if (player->isAccessPlayer()) {
		static const std::string gmOutfitName = "Gamemaster";
		protocolOutfits.emplace_back(gmOutfitName, 75, 0);
		
		static const std::string csOutfitName = "Customer Support";
		protocolOutfits.emplace_back(csOutfitName, 266, 0);

		static const std::string cmOutfitName = "Community Manager";
		protocolOutfits.emplace_back(cmOutfitName, 302, 0);
	}

	const auto& outfits = Outfits::getInstance().getOutfits(player->getSex());
	protocolOutfits.reserve(outfits.size());
	for (const Outfit& outfit : outfits) {
		uint8_t addons;
		if (!player->getOutfitAddons(outfit, addons)) {
			continue;
		}
		protocolOutfits.emplace_back(outfit.name, outfit.lookType, addons);
	}

	playermsg.write<uint16_t>(protocolOutfits.size());
	for (const ProtocolOutfit& outfit : protocolOutfits) {
		playermsg.write<uint16_t>(outfit.lookType);
		playermsg.writeString(outfit.name);
		playermsg.writeByte(outfit.addons);
		playermsg.writeByte(0x00);
	}

	#if GAME_FEATURE_MOUNTS > 0
	std::vector<const Mount*> mounts;
	for (const Mount& mount : g_game().mounts.getMounts()) {
		if (player->hasMount(&mount)) {
			mounts.push_back(&mount);
		}
	}
	
	playermsg.write<uint16_t>(mounts.size());
	for (const Mount* mount : mounts) {
		playermsg.write<uint16_t>(mount->clientId);
		playermsg.writeString(mount->name);
		playermsg.writeByte(0x00);
	}
	#endif
	
	playermsg.writeByte(0x00);//Try outfit
	playermsg.writeByte(mounted ? 0x01 : 0x00);
	writeToOutputBuffer();
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus)
{
	playermsg.reset();
	#if GAME_FEATURE_VIP_STATUS > 0
	playermsg.writeByte(0xD3);
	playermsg.write<uint32_t>(guid);
	playermsg.writeByte(newStatus);
	#else
	playermsg.writeByte(newStatus == VIPSTATUS_OFFLINE ? 0xD4 : 0xD3);
	playermsg.write<uint32_t>(guid);
	#endif
	writeToOutputBuffer();
}

#if GAME_FEATURE_ADDITIONAL_VIPINFO > 0
void ProtocolGame::sendVIP(uint32_t guid, const std::string& name, const std::string& description, uint32_t icon, bool notify, VipStatus_t status)
#else
void ProtocolGame::sendVIP(uint32_t guid, const std::string& name, VipStatus_t status)
#endif
{
	playermsg.reset();
	playermsg.writeByte(0xD2);
	playermsg.write<uint32_t>(guid);
	playermsg.writeString(name);
	#if GAME_FEATURE_ADDITIONAL_VIPINFO > 0
	playermsg.writeString(description);
	playermsg.write<uint32_t>(std::min<uint32_t>(10, icon));
	playermsg.writeByte(notify ? 0x01 : 0x00);
	#endif
	playermsg.writeByte(status);
	#if GAME_FEATURE_VIP_GROUPS > 0
	playermsg.writeByte(0x00);
	#endif
	writeToOutputBuffer();
}

void ProtocolGame::sendVIPEntries()
{
	std::stringExtended query(256);
	#if GAME_FEATURE_ADDITIONAL_VIPINFO > 0
	query << "SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name`, `description`, `icon`, `notify` FROM `account_viplist` WHERE `account_id` = " << player->getAccount();
	#else
	query << "SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name` FROM `account_viplist` WHERE `account_id` = " << player->getAccount();
	#endif

	using ProtocolGameWeak_ptr = std::weak_ptr<ProtocolGame>;
	ProtocolGameWeak_ptr protocolGameWeak = std::weak_ptr<ProtocolGame>(getThis());

	std::function<void(DBResult_ptr, bool)> callback = [protocolGameWeak](DBResult_ptr result, bool) {
		if (auto client = protocolGameWeak.lock()) {
			if (result && !client->isConnectionExpired() && client->player) {
				do {
					VipStatus_t vipStatus = VIPSTATUS_ONLINE;
					uint32_t vipGuid = result->getNumber<uint32_t>("player_id");

					Player* vipPlayer = g_game().getPlayerByGUID(vipGuid);
					if (!vipPlayer || vipPlayer->isInGhostMode() || client->player->isAccessPlayer()) {
						vipStatus = VIPSTATUS_OFFLINE;
					}

					#if GAME_FEATURE_ADDITIONAL_VIPINFO > 0
					client->sendVIP(vipGuid, result->getString("name"), result->getString("description"), result->getNumber<uint32_t>("icon"), (result->getNumber<uint16_t>("notify") != 0), vipStatus);
					#else
					client->sendVIP(vipGuid, result->getString("name"), vipStatus);
					#endif
				} while (result->next());
			}
		}
	};
	g_databaseTasks().addTask(std::move(static_cast<std::string&>(query)), callback, true);
}

void ProtocolGame::sendSpellCooldown(uint8_t spellId, uint32_t time)
{
	playermsg.reset();
	playermsg.writeByte(0xA4);
	playermsg.writeByte(spellId);
	playermsg.write<uint32_t>(time);
	writeToOutputBuffer();
}

void ProtocolGame::sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time)
{
	playermsg.reset();
	playermsg.writeByte(0xA5);
	playermsg.writeByte(groupId);
	playermsg.write<uint32_t>(time);
	writeToOutputBuffer();
}

void ProtocolGame::sendModalWindow(const ModalWindow& modalWindow)
{
	playermsg.reset();
	playermsg.writeByte(0xFA);

	playermsg.write<uint32_t>(modalWindow.id);
	playermsg.writeString(modalWindow.title);
	playermsg.writeString(modalWindow.message);

	playermsg.writeByte(modalWindow.buttons.size());
	for (const auto& it : modalWindow.buttons) {
		playermsg.writeString(it.first);
		playermsg.writeByte(it.second);
	}

	playermsg.writeByte(modalWindow.choices.size());
	for (const auto& it : modalWindow.choices) {
		playermsg.writeString(it.first);
		playermsg.writeByte(it.second);
	}

	playermsg.writeByte(modalWindow.defaultEscapeButton);
	playermsg.writeByte(modalWindow.defaultEnterButton);
	playermsg.writeByte(modalWindow.priority ? 0x01 : 0x00);

	writeToOutputBuffer();
}

////////////// Add common messages
void ProtocolGame::AddCreature(const Creature* creature, bool known, uint32_t remove)
{
  if (!creature) return;

  CreatureType_t creatureType = creature->getType();

  const Player* otherPlayer = creature->getPlayer();
  if (known) {
    playermsg.write<uint16_t>(0x62);
    playermsg.write<uint32_t>(creature->getID());
  }
  else {
    playermsg.write<uint16_t>(0x61);
    playermsg.write<uint32_t>(remove);
    playermsg.write<uint32_t>(creature->getID());
    playermsg.writeByte(creatureType);
    playermsg.writeString(creature->getName());
  }
  playermsg.writeByte(std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));

  playermsg.writeByte(creature->getDirection());
  if (!creature->isInGhostMode() && !creature->isInvisible()) {
    const Outfit_t& outfit = creature->getCurrentOutfit();
    AddOutfit(outfit);
  }
  else {
    static Outfit_t outfit;
    AddOutfit(outfit);
  }

  LightInfo lightInfo = creature->getCreatureLight();
  playermsg.writeByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
  playermsg.writeByte(lightInfo.color);

#if GAME_FEATURE_NEWSPEED_LAW > 0
  playermsg.write<uint16_t>(creature->getStepSpeed() / 2);
#else
  playermsg.write<uint16_t>(creature->getStepSpeed());
#endif

  // TODO: sync with 1240 protocol
  // #if CLIENT_VERSION >= 1240
  // playermsg.writeByte(0);//icons
  // #endif

  playermsg.writeByte(player->getSkullClient(creature));
  playermsg.writeByte(player->getPartyShield(otherPlayer));
#if GAME_FEATURE_GUILD_EMBLEM > 0
  if (!known) {
    playermsg.writeByte(player->getGuildEmblem(otherPlayer));
  }
#endif

#if GAME_FEATURE_CREATURE_TYPE > 0
  if (creatureType == CREATURETYPE_MONSTER) {
    const Creature* master = creature->getMaster();
    if (master) {
      const Player* masterPlayer = master->getPlayer();
      if (masterPlayer) {
        if (masterPlayer == player) {
          creatureType = CREATURETYPE_SUMMON_OWN;
        }
        else {
          creatureType = CREATURETYPE_SUMMON_OTHERS;
        }
      }
    }
  }

  if (creatureType == CREATURETYPE_SUMMON_OTHERS) {
    creatureType = CREATURETYPE_SUMMON_OWN;
  }
  playermsg.writeByte(creatureType);
  if (creatureType == CREATURETYPE_SUMMON_OWN) {
    const Creature* master = creature->getMaster();
    if (master) {
      playermsg.write<uint32_t>(master->getID());
    }
    else {
      playermsg.write<uint32_t>(0);
    }
  }
  else if (creatureType == CREATURETYPE_PLAYER) {
    playermsg.writeByte(creature->getPlayer()->getVocation()->getClientId());
  }
#endif

#if GAME_FEATURE_CREATURE_ICONS > 0
  playermsg.writeByte(creature->getSpeechBubble());
#endif
#if GAME_FEATURE_CREATURE_MARK > 0
  playermsg.writeByte(0xFF); // MARK_UNMARKED
#endif
#if GAME_FEATURE_INSPECTION > 0
  playermsg.writeByte(0); // inspection type
#endif

  playermsg.writeByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
}

void ProtocolGame::AddPlayerStats()
{
	playermsg.writeByte(0xA0);

	#if GAME_FEATURE_DOUBLE_HEALTH > 0
	playermsg.write<uint32_t>(player->getHealth());
	playermsg.write<uint32_t>(player->getMaxHealth());
	#else
	playermsg.write<uint16_t>(std::min<int32_t>(player->getHealth(), std::numeric_limits<uint16_t>::max()));
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMaxHealth(), std::numeric_limits<uint16_t>::max()));
	#endif

	#if GAME_FEATURE_DOUBLE_CAPACITY > 0
	playermsg.write<uint32_t>(player->getFreeCapacity());
	#else
	playermsg.write<uint16_t>(player->getFreeCapacity());
	#endif

	#if GAME_FEATURE_DOUBLE_EXPERIENCE > 0
	playermsg.write<uint64_t>(player->getExperience());
	#else
	playermsg.write<uint32_t>(std::min<uint64_t>(player->getExperience(), std::numeric_limits<uint32_t>::max()));
	#endif

	playermsg.write<uint16_t>(player->getLevel());
	playermsg.writeByte(player->getLevelPercent());

	#if GAME_FEATURE_EXPERIENCE_BONUS > 0
	#if GAME_FEATURE_DETAILED_EXPERIENCE_BONUS > 0
	playermsg.write<uint16_t>(100); // base xp gain rate
	playermsg.write<uint16_t>(0); // low level bonus
	playermsg.write<uint16_t>(0); // xp boost
	playermsg.write<uint16_t>(100); // stamina multiplier (100 = x1.0)
	#else
	playermsg.addDouble(0.0);
	#endif
	#endif

	#if GAME_FEATURE_DOUBLE_HEALTH > 0
	playermsg.write<uint32_t>(player->getMana());
	playermsg.write<uint32_t>(player->getMaxMana());
	#else
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMana(), std::numeric_limits<uint16_t>::max()));
	playermsg.write<uint16_t>(std::min<int32_t>(player->getMaxMana(), std::numeric_limits<uint16_t>::max()));
	#endif

	playermsg.writeByte(player->getSoul());

	#if GAME_FEATURE_STAMINA > 0
	playermsg.write<uint16_t>(player->getStaminaMinutes());
	#endif

	#if GAME_FEATURE_BASE_SKILLS > 0
	playermsg.write<uint16_t>(player->getBaseSpeed() / 2);
	#endif

	#if GAME_FEATURE_REGENERATION_TIME > 0
	Condition* condition = player->getCondition(CONDITION_REGENERATION);
	playermsg.write<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);
	#endif

	#if GAME_FEATURE_OFFLINE_TRAINING > 0
	playermsg.write<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);
	#endif

	#if GAME_FEATURE_DETAILED_EXPERIENCE_BONUS > 0
	playermsg.write<uint16_t>(0); // xp boost time (seconds)
	playermsg.writeByte(0); // enables exp boost in the store
	#endif
}

void ProtocolGame::AddPlayerSkills()
{
	// TODO: improve OTC to accept loyalty bonus - needs to change global otbr too
	bool loyaltyBonus = player->getOperatingSystem() < CLIENTOS_OTCLIENT_LINUX;

	playermsg.writeByte(0xA1);
	playermsg.write<uint16_t>(player->getMagicLevel());
	playermsg.write<uint16_t>(player->getBaseMagicLevel());
	if (loyaltyBonus) {
		playermsg.write<uint16_t>(player->getBaseMagicLevel());//loyalty bonus
	}
	playermsg.write<uint16_t>(player->getMagicLevelPercent() * 100);

	for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		#if GAME_FEATURE_DOUBLE_SKILLS > 0
		playermsg.write<uint16_t>(player->getSkillLevel(i));
		#else
		playermsg.write<uint8_t>(std::min<uint16_t>(player->getSkillLevel(i), std::numeric_limits<uint8_t>::max()));
		#endif

		#if GAME_FEATURE_BASE_SKILLS > 0
		#if GAME_FEATURE_DOUBLE_SKILLS > 0
		playermsg.write<uint16_t>(player->getBaseSkill(i));
		#else
		playermsg.write<uint8_t>(std::min<uint16_t>(player->getBaseSkill(i), std::numeric_limits<uint8_t>::max()));
		#endif
		#endif

		#if GAME_FEATURE_DOUBLE_PERCENT_SKILLS > 0
		if (loyaltyBonus) {
			playermsg.write<uint16_t>(player->getBaseSkill(i));//loyalty bonus
		}
		playermsg.write<uint16_t>(static_cast<uint16_t>(player->getSkillPercent(i)) * 100);
		#else
		playermsg.writeByte(player->getSkillPercent(i));
		#endif
	}

	#if GAME_FEATURE_ADDITIONAL_SKILLS > 0
	for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
		#if GAME_FEATURE_DOUBLE_SKILLS > 0
		playermsg.write<uint16_t>(std::min<int32_t>(100, player->varSpecialSkills[i]));
		#else
		playermsg.write<uint8_t>(std::min<int32_t>(100, player->varSpecialSkills[i]));
		#endif
		
		#if GAME_FEATURE_BASE_SKILLS > 0
		#if GAME_FEATURE_DOUBLE_SKILLS > 0
		playermsg.write<uint16_t>(0);
		#else
		playermsg.write<uint8_t>(0);
		#endif
		#endif
	}
	#endif

	playermsg.write<uint32_t>(player->getCapacity());
	playermsg.write<uint32_t>(player->getCapacity());
}

void ProtocolGame::AddOutfit(const Outfit_t& outfit)
{
	#if GAME_FEATURE_LOOKTYPE_U16 > 0
	playermsg.write<uint16_t>(outfit.lookType);
	#else
	playermsg.writeByte(outfit.lookType);
	#endif
	if (outfit.lookType != 0) {
		playermsg.writeByte(outfit.lookHead);
		playermsg.writeByte(outfit.lookBody);
		playermsg.writeByte(outfit.lookLegs);
		playermsg.writeByte(outfit.lookFeet);
		#if GAME_FEATURE_ADDONS > 0
		playermsg.writeByte(outfit.lookAddons);
		#endif
	} else {
		playermsg.addItemId(outfit.lookTypeEx);
	}
	
	#if GAME_FEATURE_MOUNTS > 0
	playermsg.write<uint16_t>(outfit.lookMount);
	#endif
}

void ProtocolGame::AddWorldLight(LightInfo lightInfo)
{
	playermsg.writeByte(0x82);
	playermsg.writeByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	playermsg.writeByte(lightInfo.color);
}

void ProtocolGame::AddCreatureLight(const Creature* creature)
{
	LightInfo lightInfo = creature->getCreatureLight();

	playermsg.writeByte(0x8D);
	playermsg.write<uint32_t>(creature->getID());
	playermsg.writeByte((player->isAccessPlayer() ? 0xFF : lightInfo.level));
	playermsg.writeByte(lightInfo.color);
}

//tile
void ProtocolGame::RemoveTileThing(const Position& pos, uint32_t stackpos)
{
	if (stackpos >= 10) {
		return;
	}

	playermsg.writeByte(0x6C);
	playermsg.addPosition(pos);
	playermsg.writeByte(stackpos);
}

void ProtocolGame::MoveUpCreature(const Creature* creature, const Position& newPos, const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change up
	playermsg.writeByte(0xBE);

	//going to surface
	if (newPos.z == 7) {
		int32_t skip = -1;
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 5, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 3, skip); //(floor 7 and 6 already set)
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 4, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 4, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 3, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 5, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 2, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 6, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 1, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 7, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), 0, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 8, skip);
		if (skip >= 0) {
			playermsg.writeByte(skip);
			playermsg.writeByte(0xFF);
		}
	}
	//underground, going one floor up (still underground)
	else if (newPos.z > 7) {
		int32_t skip = -1;
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), oldPos.getZ() - 3, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, 3, skip);
		if (skip >= 0) {
			playermsg.writeByte(skip);
			playermsg.writeByte(0xFF);
		}
	}

	//moving up a floor up makes us out of sync
	//west
	playermsg.writeByte(0x68);
	GetMapDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 2), newPos.z, 1, CLIENT_MAP_HEIGHT);

	//north
	playermsg.writeByte(0x65);
	GetMapDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z, CLIENT_MAP_WIDTH, 1);
}

void ProtocolGame::MoveDownCreature(const Creature* creature, const Position& newPos, const Position& oldPos)
{
	if (creature != player) {
		return;
	}

	//floor change down
	playermsg.writeByte(0xBF);

	//going from surface to underground
	if (newPos.z == 8) {
		int32_t skip = -1;

		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, -1, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z + 1, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, -2, skip);
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z + 2, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, -3, skip);
		if (skip >= 0) {
			playermsg.writeByte(skip);
			playermsg.writeByte(0xFF);
		}
	}
	//going further down
	else if (newPos.z > oldPos.z && newPos.z > 8 && newPos.z < 14) {
		int32_t skip = -1;
		GetFloorDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y - (CLIENT_MAP_HEIGHT_OFFFSET - 1), newPos.z + 2, CLIENT_MAP_WIDTH, CLIENT_MAP_HEIGHT, -3, skip);
		if (skip >= 0) {
			playermsg.writeByte(skip);
			playermsg.writeByte(0xFF);
		}
	}

	//moving down a floor makes us out of sync
	//east
	playermsg.writeByte(0x66);
	GetMapDescription(oldPos.x + CLIENT_MAP_WIDTH_OFFSET, oldPos.y - CLIENT_MAP_HEIGHT_OFFFSET, newPos.z, 1, CLIENT_MAP_HEIGHT);

	//south
	playermsg.writeByte(0x67);
	GetMapDescription(oldPos.x - (CLIENT_MAP_WIDTH_OFFSET - 1), oldPos.y + CLIENT_MAP_HEIGHT_OFFFSET, newPos.z, CLIENT_MAP_WIDTH, 1);
}

void ProtocolGame::AddShopItem(const ShopInfo& item)
{
	const ItemType& it = Item::items[item.itemId];
	playermsg.write<uint16_t>(it.clientId);
	if (it.isSplash() || it.isFluidContainer()) {
		playermsg.writeByte(serverFluidToClient(item.subType));
	} else {
		playermsg.writeByte(0x00);
	}

	playermsg.writeString(item.realName);
	playermsg.write<uint32_t>(it.weight);
	playermsg.write<uint32_t>(item.buyPrice);
	playermsg.write<uint32_t>(item.sellPrice);
}

void ProtocolGame::AddItem(uint16_t id, uint8_t count)
{
	const ItemType& it = Item::items[id];

	playermsg.write<uint16_t>(it.clientId);

	if (it.stackable) {
		playermsg.writeByte(count);
	} else if (it.isSplash() || it.isFluidContainer()) {
		playermsg.writeByte(serverFluidToClient(count));
	}

	#if GAME_FEATURE_ITEM_ANIMATION_PHASES > 0
	if (it.isAnimation) {
		playermsg.writeByte(0xFE); // random phase (0xFF for async)
	}
	#endif
}

void ProtocolGame::AddItem(const Item* item)
{
	const ItemType& it = Item::items[item->getID()];

	playermsg.write<uint16_t>(it.clientId);
	
	if (it.stackable) {
		playermsg.writeByte(std::min<uint16_t>(0xFF, item->getItemCount()));
	} else if (it.isSplash() || it.isFluidContainer()) {
		playermsg.writeByte(serverFluidToClient(item->getFluidType()));
	}

	#if GAME_FEATURE_ITEM_ANIMATION_PHASES > 0
	if (it.isAnimation) {
		playermsg.writeByte(0xFE); // random phase (0xFF for async)
	}
	#endif
}

void ProtocolGame::parseExtendedOpcode()
{
	uint8_t opcode = input_msg.readByte();
	const std::string buffer = input_msg.readString();

	// process additional opcodes via lua script event
	g_game().playerExtendedOpcode(player, opcode, buffer);
}

SpeakClasses ProtocolGame::translateSpeakClassFromClient(uint8_t talkType)
{
	switch (talkType) {
		case 0x01: return TALKTYPE_SAY;
		case 0x02: return TALKTYPE_WHISPER;
		case 0x03: return TALKTYPE_YELL;
		case 0x04: return TALKTYPE_PRIVATE_FROM;
		case 0x05: return TALKTYPE_PRIVATE_TO;
		case 0x06: return TALKTYPE_CHANNEL_M;
		case 0x07: return TALKTYPE_CHANNEL_Y;
		case 0x08: return TALKTYPE_CHANNEL_O;
		case 0x09: return TALKTYPE_SPELL;
		case 0x0A: return TALKTYPE_PRIVATE_NP;
		case 0x0C: return TALKTYPE_PRIVATE_PN;
		case 0x0D: return TALKTYPE_BROADCAST;
		case 0x0E: return TALKTYPE_CHANNEL_R1;
		case 0x0F: return TALKTYPE_PRIVATE_RED_FROM;
		case 0x10: return TALKTYPE_PRIVATE_RED_TO;
		case 0x24: return TALKTYPE_MONSTER_SAY;
		case 0x25: return TALKTYPE_MONSTER_YELL;
		default: return TALKTYPE_NONE;
	}
}

uint8_t ProtocolGame::translateSpeakClassToClient(SpeakClasses talkType)
{
	switch (talkType) {
		case TALKTYPE_SAY: return 0x01;
		case TALKTYPE_WHISPER: return 0x02;
		case TALKTYPE_YELL: return 0x03;
		case TALKTYPE_PRIVATE_FROM: return 0x04;
		case TALKTYPE_PRIVATE_TO: return 0x05;
		case TALKTYPE_CHANNEL_M: return 0x06;
		case TALKTYPE_CHANNEL_Y: return 0x07;
		case TALKTYPE_CHANNEL_O: return 0x08;
		case TALKTYPE_SPELL: return 0x09;
		case TALKTYPE_PRIVATE_NP: return 0x0A;
		case TALKTYPE_PRIVATE_PN: return 0x0C;
		case TALKTYPE_BROADCAST: return 0x0D;
		case TALKTYPE_CHANNEL_R1: return 0x0E;
		case TALKTYPE_PRIVATE_RED_FROM: return 0x0F;
		case TALKTYPE_PRIVATE_RED_TO: return 0x10;
		case TALKTYPE_MONSTER_SAY: return 0x24;
		case TALKTYPE_MONSTER_YELL: return 0x25;
		case TALKTYPE_BOOSTED_CREATURE: return 0x31;
		default: return TALKTYPE_NONE;
	}
}

uint8_t ProtocolGame::translateMessageClassToClient(MessageClasses messageType)
{
	switch (messageType) {
		case MESSAGE_STATUS_CONSOLE_BLUE: return 0x04;
		case MESSAGE_STATUS_CONSOLE_RED: return 0x0D;
		case MESSAGE_STATUS_DEFAULT: return 0x11;
		case MESSAGE_STATUS_WARNING: return 0x12;
		case MESSAGE_EVENT_ADVANCE: return 0x13;
		case MESSAGE_STATUS_SMALL: return 0x15;
		case MESSAGE_INFO_DESCR: return 0x16;
		case MESSAGE_EVENT_DEFAULT: return 0x1E;
		case MESSAGE_GUILD: return 0x21;
		case MESSAGE_PARTY_MANAGEMENT: return 0x22;
		case MESSAGE_PARTY: return 0x23;
		case MESSAGE_EVENT_ORANGE: return 0x24;
		case MESSAGE_STATUS_CONSOLE_ORANGE: return 0x25;
		case MESSAGE_DAMAGE_DEALT: return 0x17;
		case MESSAGE_DAMAGE_RECEIVED: return 0x18;
		case MESSAGE_MANA: return 0x2B;
		case MESSAGE_HEALED: return 0x19;
		case MESSAGE_EXPERIENCE: return 0x1A;
		case MESSAGE_DAMAGE_OTHERS: return 0x1B;
		case MESSAGE_HEALED_OTHERS: return 0x1C;
		case MESSAGE_EXPERIENCE_OTHERS: return 0x1D;
		case MESSAGE_LOOT: return 0x1F;
		case MESSAGE_LOGIN: return 0x11;
		case MESSAGE_WARNING: return 0x12;
		case MESSAGE_GAME: return 0x13;
		case MESSAGE_GAME_HIGHLIGHT: return 0x14;
		case MESSAGE_FAILURE: return 0x15;
		case MESSAGE_LOOK: return 0x16;
		case MESSAGE_STATUS: return 0x1E;
		case MESSAGE_TRADENPC: return 0x20;
		case MESSAGE_REPORT: return 0x26;
		case MESSAGE_HOTKEY: return 0x27;
		case MESSAGE_TUTORIAL: return 0x28;
		case MESSAGE_THANKYOU: return 0x29;
		case MESSAGE_MARKET: return 0x2A;
		default: return MESSAGE_NONE;
	}
}

void ProtocolGame::sendCreature(const Creature* creature, Position pos, bool cleanTile)
{
  if (!creature || !player->canSeeCreature(creature)) {
    return;
  }

  bool known;
  uint32_t remove;
  checkCreatureAsKnown(creature->getID(), known, remove);

  Wrapper_ptr wrapper = getOutputBuffer();
  flatbuffers::FlatBufferBuilder& fbb = wrapper->Builder();
  auto name = fbb.CreateString(creature->isHealthHidden() ? std::string() : creature->getName());

  // Add creatures identifications
  CanaryLib::CreatureDataBuilder creature_builder(fbb);
  creature_builder.add_id(creature->getID());
  creature_builder.add_remove_id(remove);
  // TODO move getType to CanaryLib::CreatureType_t
  creature_builder.add_type(static_cast<CanaryLib::CreatureType_t>(creature->getType()));

  // Add creature descriptive info
  creature_builder.add_name(name);
  creature_builder.add_direction(creature->getDirection());
  creature_builder.add_health_percent(creature->isHealthHidden() && creature != player
    ? 0x00
    : std::ceil((static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100)
  );
  creature_builder.add_speed(creature->getStepSpeed() / 2);
  creature_builder.add_walkable(player->canWalkthroughEx(creature) ? 0x00 : 0x01);

  // Add light - TODO move getCreatureLight to CanaryLib::Light
  LightInfo light = creature->getCreatureLight();
  CanaryLib::Light _light(light.color, player->isAccessPlayer() ? 0xFF : light.level);
  creature_builder.add_light(&_light);

  // Add outfit - TODO move getCurrentOutfit to CanaryLib::Outfit
  Outfit_t outfit;
  if (!creature->isInGhostMode() && !creature->isInvisible()) {
    outfit = creature->getCurrentOutfit();
  }
  CanaryLib::Outfit _outfit(
    outfit.lookType,
    outfit.lookBody,
    outfit.lookFeet,
    outfit.lookHead,
    outfit.lookLegs,
    outfit.lookAddons,
    outfit.lookMount,
    outfit.lookTypeEx
  );
  creature_builder.add_outfit(&_outfit);

  const Player* otherPlayer = creature->getPlayer();

  creature_builder.add_guild_emblem(player->getGuildEmblem(!known ? otherPlayer : nullptr));
  creature_builder.add_party_shield(player->getPartyShield(otherPlayer));
  creature_builder.add_icon(creature->getSpeechBubble());
  creature_builder.add_skull(player->getSkullClient(creature));
  creature_builder.add_square_mark(0xFF);

  if (Creature* master = creature->getMaster()) {
    creature_builder.add_master_id(master->getID());
  }

  const CanaryLib::Position central_pos{ pos.x, pos.y, pos.z };
  auto thing_data = CanaryLib::CreateThingData(fbb, CanaryLib::Thing_CreatureData, creature_builder.Finish().Union(), &central_pos, cleanTile);
  wrapper->add(thing_data.Union(), CanaryLib::DataType_ThingData);
}

void ProtocolGame::sendItem(const Item* item, Position pos, bool cleanTile)
{
  if (!item) return;

  Wrapper_ptr wrapper = getOutputBuffer();
  flatbuffers::FlatBufferBuilder& fbb = wrapper->Builder();

  auto item_data = fbb.CreateStruct(CanaryLib::ItemData{ Item::items[item->getID()].clientId, item->getItemCount(), item->getFluidType() });

  const CanaryLib::Position central_pos{ pos.x, pos.y, pos.z };
  auto thing_data = CanaryLib::CreateThingData(fbb, CanaryLib::Thing_ItemData, item_data.Union(), &central_pos, cleanTile);
  wrapper->add(thing_data.Union(), CanaryLib::DataType_ThingData);
}
