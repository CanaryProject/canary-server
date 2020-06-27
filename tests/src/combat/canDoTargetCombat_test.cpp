#include "../main.hpp"

extern LuaEnvironment g_luaEnvironment;

TEST_CASE( "CombatTest - canDoTargetCombat" ) {
    CombatParams params;
    MonsterType type;
    Player player(nullptr);
    Monster *monsterA = new Monster(&type);
    Monster *monsterB = new Monster(&type);

	SECTION("Monster cannot attack monster") {
    CHECK(Combat::canDoTargetCombat(monsterA, monsterB, params) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	SECTION("Player cannot attack monster") {
    CHECK(Combat::canDoTargetCombat(&player, monsterA, params) == RETURNVALUE_NOERROR);
  }

  delete monsterA;
  delete monsterB;
} 