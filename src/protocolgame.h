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

#ifndef FS_PROTOCOLGAME_H_FACA2A2D1A9348B78E8FD7E8003EBB87
#define FS_PROTOCOLGAME_H_FACA2A2D1A9348B78E8FD7E8003EBB87

#include "protocol.h"
#include "chat.h"
#include "creature.h"
#include "tasks.h"

class NetworkMessage;
class Player;
class Game;
class House;
class Container;
class Tile;
class Connection;
class Quest;
class ProtocolGame;
using ProtocolGame_ptr = std::shared_ptr<ProtocolGame>;

struct TextMessage
{
	MessageClasses type = MESSAGE_STATUS_DEFAULT;
	std::string text;
	Position position;
	uint16_t channelId;
	struct {
		int32_t value = 0;
		Color_t color;
	} primary, secondary;

	TextMessage() = default;
	TextMessage(MessageClasses type, std::string text) : type(type), text(std::move(text)) {}
};

#if GAME_FEATURE_QUEST_TRACKER > 0
class Mission;
#endif

class ProtocolGame final : public Protocol
{
	public:
		static const CanaryLib::Protocol_t id() {
			return CanaryLib::Protocol_t_PROTOCOL_GAME;
		}

		static const char* protocol_name() {
			return "gameworld protocol";
		}

		explicit ProtocolGame() {}

		#if GAME_FEATURE_SESSIONKEY > 0
		void login(const std::string& accountName, const std::string& password, std::string& characterName, std::string& token, uint32_t tokenTime, OperatingSystem_t operatingSystem, OperatingSystem_t tfcOperatingSystem);
		#else
		void login(const std::string& accountName, const std::string& password, std::string& characterName, OperatingSystem_t operatingSystem, OperatingSystem_t tfcOperatingSystem);
		#endif
		void logout(bool displayEffect, bool forced);

		NetworkMessage playermsg;
		NetworkMessage input_msg;

  protected:
    // Flatbuffer
		void parseLoginInfo(const CanaryLib::LoginInfo *login_info) override;
    bool validateLoginChallenge(const CanaryLib::Challenge *challenge) override {
      if (challenge && (challenge->timestamp() != challengeTimestamp || challenge->random() != challengeRandom)) {
        return false;
      }
      return true;
    }

	private:
		ProtocolGame_ptr getThis() {
			return std::static_pointer_cast<ProtocolGame>(shared_from_this());
		}
		void connect(uint32_t playerId, OperatingSystem_t operatingSystem, OperatingSystem_t tfcOperatingSystem);
		void disconnectClient(const std::string& message) const override;
		void writeToOutputBuffer(NetworkMessage& msg);
		void writeToOutputBuffer();

		void release() override;

		void checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown);

		bool canSee(int32_t x, int32_t y, int32_t z) const;
		bool canSee(const Creature*) const;
		bool canSee(const Position& pos) const;

		// we have all the parse methods
		void parsePacket(NetworkMessage& msg) override;
		void onConnect() override;

		//Parse methods
		#if GAME_FEATURE_QUEST_TRACKER > 0
		void parseTrackedQuestFlags();
		#endif
		void parseAutoWalk();
		void parseSetOutfit();
		void parseSay();
		void parseWrapableItem();
		void parseLookAt();
		void parseLookInBattleList();
		void parseFightModes();
		void parseAttack();
		void parseFollow();
		void parseEquipObject();
		void parseTeleport();

		void parseCyclopediaMonsters();
		void parseCyclopediaRace();
		void parseCyclopediaHouseAction();
		void parseCyclopediaCharacterInfo();

		void parseTournamentLeaderboard();

		void parseBugReport();
		void parseDebugAssert();
		void parseRuleViolationReport();

		void parseThrow();
		void parseUseItemEx();
		void parseUseWithCreature();
		void parseUseItem();
		void parseCloseContainer();
		void parseUpArrowContainer();
		void parseUpdateContainer();
		void parseTextWindow();
		void parseHouseWindow();

		void parseLookInShop();
		void parsePlayerPurchase();
		void parsePlayerSale();

		void parseQuestLine();

		void parseInviteToParty();
		void parseJoinParty();
		void parseRevokePartyInvite();
		void parsePassPartyLeadership();
		void parseEnableSharedPartyExperience();

		#if GAME_FEATURE_MOUNTS > 0
		void parseToggleMount();
		#endif
		void parseModalWindowAnswer();

		#if GAME_FEATURE_BROWSEFIELD > 0
		void parseBrowseField();
		#endif
		#if GAME_FEATURE_CONTAINER_PAGINATION > 0
		void parseSeekInContainer();
		#endif
		#if GAME_FEATURE_INSPECTION > 0
		void parseInspectionObject();
		#endif

		//trade methods
		void parseRequestTrade();
		void parseLookInTrade();

		#if GAME_FEATURE_MARKET > 0
		//market methods
		void parseMarketLeave();
		void parseMarketBrowse();
		void parseMarketCreateOffer();
		void parseMarketCancelOffer();
		void parseMarketAcceptOffer();
		#endif

		//VIP methods
		void parseAddVip();
		void parseRemoveVip();
		void parseEditVip();

		void parseRotateItem();

		//Channel tabs
		void parseChannelInvite();
		void parseChannelExclude();
		void parseOpenChannel();
		void parseOpenPrivateChannel();
		void parseCloseChannel();

		//Send functions
		#if GAME_FEATURE_INSPECTION > 0
		void sendItemInspection(uint16_t itemId, uint8_t itemCount, const Item* item, bool cyclopedia);
		#endif
		void sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel);
		#if GAME_FEATURE_CHAT_PLAYERLIST > 0
		void sendChannelEvent(uint16_t channelId, const std::string& playerName, ChannelEvent_t channelEvent);
		#endif
		void sendClosePrivate(uint16_t channelId);
		void sendCreatePrivateChannel(uint16_t channelId, const std::string& channelName);
		void sendChannelsDialog();
		void sendChannel(uint16_t channelId, const std::string& channelName, const UsersMap* channelUsers, const InvitedMap* invitedUsers);
		void sendOpenPrivateChannel(const std::string& receiver);
		void sendToChannel(const Creature* creature, SpeakClasses type, const std::string& text, uint16_t channelId);
		void sendPrivateMessage(const Player* speaker, SpeakClasses type, const std::string& text);
		void sendIcons(uint32_t icons);
		void sendFYIBox(const std::string& message);

		void sendDistanceShoot(const Position& from, const Position& to, uint8_t type);
		void sendMagicEffect(const Position& pos, uint8_t type);
		void sendCreatureHealth(const Creature* creature, uint8_t healthPercent);
		#if GAME_FEATURE_PARTY_LIST > 0
		void sendPartyCreatureUpdate(const Creature* target);
		void sendPartyCreatureShield(const Creature* target);
		void sendPartyCreatureSkull(const Creature* target);
		void sendPartyCreatureHealth(const Creature* target, uint8_t healthPercent);
		void sendPartyPlayerMana(const Player* target, uint8_t manaPercent);
		void sendPartyCreatureShowStatus(const Creature* target, bool showStatus);
		#endif
		void sendSkills();
		void sendPing();
		void sendPingBack();
		void sendCreatureTurn(const Creature* creature, uint32_t stackPos);
		void sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text, const Position* pos = nullptr);

		void sendQuestLog();
		void sendQuestLine(const Quest* quest);
		#if GAME_FEATURE_QUEST_TRACKER > 0
		void sendTrackedQuests(uint8_t remainingQuests, std::vector<uint16_t>& quests);
		void sendUpdateTrackedQuest(const Mission* mission);
		#endif

		void sendCancelWalk();
		void sendChangeSpeed(const Creature* creature, uint32_t speed);
		void sendCancelTarget();
		void sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit);
		void sendStats();
		void sendBasicData();
		//void sendBlessStatus();
		void sendTextMessage(const TextMessage& message);
		void sendReLoginWindow(uint8_t unfairFightReduction);

		void sendTutorial(uint8_t tutorialId);
		void sendAddMarker(const Position& pos, uint8_t markType, const std::string& desc);

		void sendMonsterCyclopedia();
		void sendCyclopediaMonsters(const std::string& race);
		void sendCyclopediaRace(uint16_t monsterId);
		void sendCyclopediaBonusEffects();
		void sendCyclopediaCharacterBaseInformation();
		void sendCyclopediaCharacterGeneralStats();
		void sendCyclopediaCharacterCombatStats();
		void sendCyclopediaCharacterRecentDeaths();
		void sendCyclopediaCharacterRecentPvPKills();
		void sendCyclopediaCharacterAchievements();
		void sendCyclopediaCharacterItemSummary();
		void sendCyclopediaCharacterOutfitsMounts();
		void sendCyclopediaCharacterStoreSummary();
		void sendCyclopediaCharacterInspection();
		void sendCyclopediaCharacterBadges();
		void sendCyclopediaCharacterTitles();

		void sendTournamentLeaderboard();

		void updateCreatureData(const Creature* creature);
		void sendCreatureWalkthrough(const Creature* creature, bool walkthrough);
		void sendCreatureShield(const Creature* creature);
		void sendCreatureSkull(const Creature* creature);
		void sendCreatureType(const Creature* creature, uint8_t creatureType);

		void sendShop(Npc* npc, const ShopInfoList& itemList);
		void sendCloseShop();
		void sendSaleItemList(const std::vector<ShopInfo>& shop, const std::map<uint32_t, uint32_t>& inventoryMap);
		#if GAME_FEATURE_MARKET > 0
		void sendMarketEnter(uint32_t depotId);
		void sendMarketLeave();
		void sendMarketBrowseItem(uint16_t itemId, const MarketOfferList& buyOffers, const MarketOfferList& sellOffers);
		void sendMarketAcceptOffer(const MarketOfferEx& offer);
		void sendMarketBrowseOwnOffers(const MarketOfferList& buyOffers, const MarketOfferList& sellOffers);
		void sendMarketCancelOffer(const MarketOfferEx& offer);
		void sendMarketBrowseOwnHistory(const HistoryMarketOfferList& buyOffers, const HistoryMarketOfferList& sellOffers);
		void sendMarketDetail(uint16_t itemId);
		#endif
		void sendTradeItemRequest(const std::string& traderName, const Item* item, bool ack);
		void sendCloseTrade();

		void sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite);
		void sendTextWindow(uint32_t windowTextId, uint32_t itemId, const std::string& text);
		void sendHouseWindow(uint32_t windowTextId, const std::string& text);
		void sendOutfitWindow();

		void sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus);
		#if GAME_FEATURE_ADDITIONAL_VIPINFO > 0
		void sendVIP(uint32_t guid, const std::string& name, const std::string& description, uint32_t icon, bool notify, VipStatus_t status);
		#else
		void sendVIP(uint32_t guid, const std::string& name, VipStatus_t status);
		#endif
		void sendVIPEntries();
		void sendFightModes();

		void sendCreatureLight(const Creature* creature);
		void sendWorldLight(LightInfo lightInfo);
		void sendTibiaTime(int32_t time);

		void sendCreatureSquare(const Creature* creature, Color_t color);

		void sendSpellCooldown(uint8_t spellId, uint32_t time);
		void sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time);

		//tiles
		void sendMapDescription(const Position& pos);

		#if GAME_FEATURE_TILE_ADDTHING_STACKPOS > 0
		void sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item);
		#else
		void sendAddTileItem(const Position& pos, const Item* item);
		#endif
		void sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item);
		void sendRemoveTileThing(const Position& pos, uint32_t stackpos);
		void sendUpdateTile(const Tile* tile, const Position& pos);

		void sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos, bool isLogin);
		void sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos,
		                      const Position& oldPos, int32_t oldStackPos, bool teleport);

		//containers
		#if GAME_FEATURE_CONTAINER_PAGINATION > 0
		void sendAddContainerItem(uint8_t cid, uint16_t slot, const Item* item);
		void sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item);
		void sendRemoveContainerItem(uint8_t cid, uint16_t slot, const Item* lastItem);
		void sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex);
		#else
		void sendAddContainerItem(uint8_t cid, const Item* item);
		void sendUpdateContainerItem(uint8_t cid, uint8_t slot, const Item* item);
		void sendRemoveContainerItem(uint8_t cid, uint8_t slot);
		void sendContainer(uint8_t cid, const Container* container, bool hasParent);
		#endif
		void sendCloseContainer(uint8_t cid);

		//inventory
		void sendInventoryItem(slots_t slot, const Item* item);
		#if GAME_FEATURE_INVENTORY_LIST > 0
		void sendItems(const std::map<uint32_t, uint32_t>& inventoryMap);
		#endif

		//messages
		void sendModalWindow(const ModalWindow& modalWindow);

		//Help functions

		// translate a tile to clientreadable format
		void GetTileDescription(const Tile* tile);

		// translate a floor to clientreadable format
		void GetFloorDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, int32_t offset, int32_t& skip);

		// translate a map area to clientreadable format
		void GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height);

		void AddCreature(const Creature* creature, bool known, uint32_t remove);
		void AddPlayerStats();
		void AddOutfit(const Outfit_t& outfit);
		void AddPlayerSkills();
		void AddWorldLight(LightInfo lightInfo);
		void AddCreatureLight(const Creature* creature);

		//tiles
		void RemoveTileThing(const Position& pos, uint32_t stackpos);

		void MoveUpCreature(const Creature* creature, const Position& newPos, const Position& oldPos);
		void MoveDownCreature(const Creature* creature, const Position& newPos, const Position& oldPos);

		//shop
		void AddShopItem(const ShopInfo& item);

		//items
		void AddItem(uint16_t id, uint8_t count);
		void AddItem(const Item* item);

		//otclient
		void parseExtendedOpcode();

		//translations
		SpeakClasses translateSpeakClassFromClient(uint8_t talkType);
		uint8_t translateSpeakClassToClient(SpeakClasses talkType);
		uint8_t translateMessageClassToClient(MessageClasses messageType);

		friend class Player;

		std::unordered_set<uint32_t> knownCreatureSet;
		Player* player = nullptr;

		uint64_t eventConnect = 0;
		uint32_t challengeTimestamp = 0;
		uint8_t challengeRandom = 0;

		bool addExivaRestrictions = false;
		bool debugAssertSent = false;
		bool acceptPackets = false;
};

#endif
