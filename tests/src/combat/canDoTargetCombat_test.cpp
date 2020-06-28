#include "../all.h"

TEST_SUITE( "CombatTest - canDoTargetCombat" ) {
	TEST_CASE("Can attack when attacker is null") {
    CHECK(Combat::canDoTargetCombat(nullptr, &monsterB, CombatParams()) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Can't attacker when isTargetValid is false (target null)") {
    CHECK(Combat::canDoTargetCombat(&monsterA, nullptr, CombatParams()) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	TEST_CASE("Monster cannot attack monster") {
    CHECK(Combat::canDoTargetCombat(&monsterA, &monsterB, CombatParams()) == RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE);
  }

	TEST_CASE("Monster can attack common player") {
    CHECK(Combat::canDoTargetCombat(&monsterA, &player, CombatParams()) == RETURNVALUE_NOERROR);
  }

	TEST_CASE("Player can attack monster") {
    CHECK(Combat::canDoTargetCombat(&player, &monsterA, CombatParams()) == RETURNVALUE_NOERROR);
  }
} 