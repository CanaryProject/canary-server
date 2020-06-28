#include "../all.h"

TEST_SUITE( "CombatTest - canDoTargetCombat" ) {
	TEST_CASE("Can attack when attacker is null") {
    CHECK(Combat::canDoTargetCombat(nullptr, &monsterB, params) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Can't attacker when isTargetValid is false (target null)") {
    CHECK(Combat::canDoTargetCombat(&monsterA, nullptr, params) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	TEST_CASE("Monster cannot attack monster") {
    CHECK(Combat::canDoTargetCombat(&monsterA, &monsterB, params) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	TEST_CASE("Monster can attack common player") {
    CHECK(Combat::canDoTargetCombat(&monsterA, &player, params) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Player can attack monster") {
    CHECK(Combat::canDoTargetCombat(&player, &monsterA, params) == RETURNVALUE_NOERROR);
  }
} 