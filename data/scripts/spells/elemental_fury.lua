-- configs
local configs = {
	cicles = 1,
	ringsDelay = 500,
	hitEffects = {
		CONST_ME_FIREAREA, -- efeito do hit anel 1
		CONST_ME_ICEATTACK, -- efeito do hit anel 2
		CONST_ME_SMALLPLANTS -- efeito do hit anel 3
	},
	damageType = {
		COMBAT_FIREDAMAGE, -- damage type anel 1
		COMBAT_ICEDAMAGE, -- damage type anel 2
		COMBAT_EARTHDAMAGE -- damage type anel 3
	},
	areas = {
		AREA_RING1X1, -- area anel 1
		AREA_RING2X2, -- area anel 2
		AREA_RING3X3 -- area anel 3
	},
	minDamage = {
		{ baseDamage = 0, levelMultiplier = 0.1, mlMultiplier = 0.3},
		{ baseDamage = 3, levelMultiplier = 0.15, mlMultiplier = 0.75},
		{ baseDamage = 7, levelMultiplier = 0.2, mlMultiplier = 1.2}
	},
	maxDamage = {
		{ baseDamage = 0, levelMultiplier = 0.1, mlMultiplier = 1.2},
		{ baseDamage = 8, levelMultiplier = 0.15, mlMultiplier = 2},
		{ baseDamage = 16, levelMultiplier = 0.2, mlMultiplier = 2.85}
	}
}

-- Combat Principal
	-- Damage function
local function getDamage(type, combat, level, magicLevel)
	local damage = configs[type][combat]
	return damage.baseDamage + (level * damage.levelMultiplier) + (magicLevel * damage.mlMultiplier)
end

	-- Formula de dano
function formulaCalculator(player, level, magicLevel, combat)
	return -getDamage('minDamage', combat, level, magicLevel), -getDamage('maxDamage', combat, level, magicLevel)
end

function onGetFormulaValues1(player, level, magicLevel)
	return formulaCalculator(player, level, magicLevel, 1)
end
function onGetFormulaValues2(player, level, magicLevel)
	return formulaCalculator(player, level, magicLevel, 2)
end
function onGetFormulaValues3(player, level, magicLevel)
	return formulaCalculator(player, level, magicLevel, 3)
end

-- Auxiliar effect combats
local ringCombats = { Combat(), Combat(), Combat() }

for i = 1, 3 do
	ringCombats[i]:setParameter(COMBAT_PARAM_TYPE, configs.damageType[i])
	ringCombats[i]:setCallback(CALLBACK_PARAM_LEVELMAGICVALUE, "onGetFormulaValues"..i)
	ringCombats[i]:setParameter(COMBAT_PARAM_EFFECT, configs.hitEffects[i])
	ringCombats[i]:setArea(createCombatArea(configs.areas[i]))
end

local spell = Spell("instant")

local function doCombat(cid, var, i)
	local creature = Creature(cid)
	if creature then
		ringCombats[i]:execute(creature, Variant(creature:getPosition()))
	end
end

function spell.onCastSpell(creature, var)
	for i = 1, 3 * configs.cicles do
		index = i % 3 == 0 and 3 or i % 3
		addEvent(doCombat, i * configs.ringsDelay, creature:getId(), var, index)
	end
end

spell:name("Elemental Fury")
spell:words("exori nature")
spell:group("attack")
spell:vocation("knight", true)
spell:id(107)
spell:cooldown(6 * 1000)
spell:groupCooldown(2 * 1000)
spell:level(28)
spell:mana(40)
spell:isPremium(true)
spell:needLearn(false)
spell:blockWalls(true)
spell:register()
