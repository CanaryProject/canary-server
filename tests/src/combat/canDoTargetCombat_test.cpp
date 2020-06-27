#include "../main.hpp"

extern LuaEnvironment g_luaEnvironment;

TEST_CASE( "CombatTest - canDoTargetCombat" ) {

	SECTION("Monster cannot attack monster") {
    CombatParams params;
    MonsterType type;
    Monster *a = new Monster(&type);
    Monster *b = new Monster(&type);

    CHECK(Combat::canDoTargetCombat(a, b, params) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);

    delete a;
    delete b;
  }
} 