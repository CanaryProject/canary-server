#include "../all.h"

LuaEnvironment g_luaEnvironment;

TEST_SUITE( "CombatTest - canDoTargetCombat" ) {
    CombatParams params;
    MonsterType type;
    Player player(nullptr);
    Monster *monsterA = new Monster(&type);
    Monster *monsterB = new Monster(&type);

	TEST_CASE("Monster cannot attack monster") {
    CHECK(Combat::canDoTargetCombat(monsterA, monsterB, params) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	TEST_CASE("Monster can attack common player") {
    CHECK(Combat::canDoTargetCombat(monsterA, &player, params) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Player can attack monster") {
    CHECK(Combat::canDoTargetCombat(&player, monsterA, params) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Free memory") {
    delete monsterA;
    delete monsterB;
  }
} 