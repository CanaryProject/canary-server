-- configs
local configs = {
	range = 5,
	hits = 20,
	hitDelay = 500,
	auraDelay = 1000,
	auraEffect = CONST_ME_MAGIC_GREEN,
	distanceEffects = { -- possiveis distance effects (aleatorio)
		CONST_ANI_GLOOTHSPEAR,
		CONST_ANI_DIAMONDARROW,
		CONST_ANI_SPECTRALBOLT,
		CONST_ANI_ROYALSTAR
	},
	minDamage = { baseDamage = 1, levelMultiplier = 0.2, skillMultiplier = 0.01, mlMultiplier = 1.0},
	maxDamage = { baseDamage = 6, levelMultiplier = 0.2, skillMultiplier = 0.02, mlMultiplier = 1.5},
}

-- Combat Principal
	-- Damage function
local function getDamage(type, skillTotal, level, magicLevel)
	local damage = configs[type]
	return damage.baseDamage + (skillTotal * damage.skillMultiplier) + (level * damage.levelMultiplier) + (magicLevel * damage.mlMultiplier)
end

	-- Formula de dano
function onGetFormulaValues(player, skill, attack, factor)
	local skillTotal = skill * attack
	local level = player:getLevel()
	local magicLevel = player:getMagicLevel()
	return -getDamage('minDamage', skillTotal, level, magicLevel), -getDamage('maxDamage', skillTotal, level, magicLevel)
end

-- Damage combat object
local dmgCombat = Combat()
dmgCombat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
dmgCombat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")

local function doCombat(cid, round, auraCooldown)
	creature = Creature(cid)
	if not creature then return end

	round = round or 1
	auraCooldown = auraCooldown or 1000
	if round >= configs.hits then return end

	local pos = creature:getPosition()

	if auraCooldown >= configs.auraDelay then
		auraCooldown = auraCooldown % configs.auraDelay
		pos:sendMagicEffect(configs.auraEffect)
	end

	local targets = Game.getSpectators(pos, false, false, configs.range, configs.range)
	if #targets > 1 then
		local target = creature
		repeat
			target = targets[math.random(1, #targets)]
		until target:getId() ~= cid

		if target then
			local tPos = target:getPosition()
			pos:sendDistanceEffect(tPos, configs.distanceEffects[math.random(1, #configs.distanceEffects)])
			dmgCombat:execute(creature, Variant(tPos))
		end
	end

	addEvent(doCombat, configs.hitDelay, creature:getId(), round + 1, auraCooldown + configs.hitDelay)
end

local spell = Spell("instant")

function spell.onCastSpell(creature, var)
	doCombat(creature:getId())
end

spell:name("frenesi")
spell:words("frenesi")
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
