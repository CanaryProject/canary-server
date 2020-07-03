-- configs
local configs = {
	duration = 12,
	hitDelay = 100,
	cicleDelay = 500,
	effects = { 
		hitEffect = CONST_ME_GROUNDSHAKER, -- efeito do hit
		shootEffect = CONST_ANI_WEAPONTYPE -- efeito do tiro
	},
	baseDamage = { min = 1, max = 6 },
	levelMultiplier = { min = 0.2, max = 0.2 },
	skillMultiplier = { min = 0.01, max = 0.03 }
}

-- Combat Principal
	-- Damage function
local function getDamage(type, skillTotal, level)
	return configs.baseDamage[type] + (skillTotal * configs.skillMultiplier[type]) + (level * configs.levelMultiplier[type])
end

	-- Formula de dano
function onGetFormulaValues(player, skill, attack, factor)
	local skillTotal = skill * attack
	local level = player:getLevel()
	return -getDamage('min', skillTotal, level), -getDamage('max', skillTotal, level)
end

-- Damage combat object
local dmgCombat = Combat()
dmgCombat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
dmgCombat:setParameter(COMBAT_PARAM_DISTANCEEFFECT, configs.effects.shootEffect)
dmgCombat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")

	-- Delayed hit effect
function dmgFunction(cid, tid)
	local target = Creature(tid)
	if target then
		dmgCombat:execute(Creature(cid), Variant(target:getPosition()))
	end
end

function onTargetCreature(creature, target)
	addEvent(dmgFunction, math.random(1, 5) * configs.hitDelay, creature:getId(), target:getId())
end
function onTargetCreature2(creature, target)
	addEvent(dmgFunction, math.random(1, 5) * configs.hitDelay, creature:getId(), target:getId())
end
function onTargetCreature3(creature, target)
	addEvent(dmgFunction, math.random(1, 5) * configs.hitDelay, creature:getId(), target:getId())
end

-- Auxiliar effect combats
local ringCombats = { Combat(), Combat(), Combat() }

for i = 1, 3 do
	ringCombats[i]:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
	ringCombats[i]:setParameter(COMBAT_PARAM_EFFECT, configs.effects.hitEffect)
	if i == 1 then
		ringCombats[i]:setArea(createCombatArea(AREA_RING1X1))
		ringCombats[i]:setCallback(CALLBACK_PARAM_TARGETCREATURE, "onTargetCreature")
	elseif i == 2 then
		ringCombats[i]:setArea(createCombatArea(AREA_RING2X2))
		ringCombats[i]:setCallback(CALLBACK_PARAM_TARGETCREATURE, "onTargetCreature2")
	else
		ringCombats[i]:setArea(createCombatArea(AREA_RING3X3))
		ringCombats[i]:setCallback(CALLBACK_PARAM_TARGETCREATURE, "onTargetCreature3")
	end
end

local spell = Spell("instant")

function combatDuration(cid, startedAt, cicle)
	if ((os.time() - startedAt) >= configs.duration) then return end

	cicle = cicle or 1
	local creature = Creature(cid)
	local combatIndex = (cicle % 3 == 0) and 3 or cicle % 3
	ringCombats[combatIndex]:execute(creature, Variant(creature:getPosition()))
	addEvent(combatDuration, configs.cicleDelay, cid, startedAt, cicle + 1)
end

function spell.onCastSpell(creature, var)
	combatDuration(creature:getId(), os.time())
end

spell:name("Whirlwind Throw")
spell:words("exori fury")
spell:group("attack")
spell:vocation("knight", true)
spell:id(107)
spell:cooldown(6 * 1000)
spell:groupCooldown(2 * 1000)
spell:level(28)
spell:mana(40)
spell:isPremium(true)
spell:range(3)
spell:needWeapon(true)
spell:needLearn(false)
spell:blockWalls(true)
spell:register()
