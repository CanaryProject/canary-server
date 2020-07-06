-- configs
local configs = {
	totalTiles = 5, -- não mexer
	waveSize = 10, -- numero de rajadas
	hitDelay = 100,
	waveDelay = 200, -- delay entre as rajadas
	firstDelay = 700, -- delay entre o hit e o inicio das rajadas
	effects = { 
		hitEffect = CONST_ME_GROUNDSHAKER, -- efeito do primeiro hit
		distanceEffect = CONST_ANI_NONE, -- distance effect do primeiro hit (opicional)
		waveEffect = CONST_ANI_ETHEREALSPEAR -- distance effect da rajada
	},
	baseDamage = { min = 1, max = 6 },
	levelMultiplier = { min = 0.2, max = 0.2 },
	skillMultiplier = { min = 0.01, max = 0.03 }
}

-- auxiliar variables
local tilesCount
local area = {}
local direction = DIRECTION_NORTH
local inversionMap = {
	[DIRECTION_NORTH] = true,
	[DIRECTION_WEST] = true,
	[DIRECTION_NORTHWEST] = true,
	[DIRECTION_NORTHEAST] = true,
	[DIRECTION_EAST] = false,
	[DIRECTION_SOUTH] = false,
	[DIRECTION_SOUTHWEST] = false,
	[DIRECTION_SOUTHEAST] = false
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

local dmgCombat = Combat()
dmgCombat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
dmgCombat:setArea(createCombatArea(AREA_LARGEBEAM1X1, AREADIAGONAL_LARGEBEAM1X1))
dmgCombat:setCallback(CALLBACK_PARAM_SKILLVALUE, "onGetFormulaValues")

local function performCombatEffects(creature)
	local bPos, ePos = area[1], area[configs.totalTiles]
	if inversionMap[direction] then bPos, ePos = ePos, bPos end

	bPos:sendDistanceEffect(ePos, configs.effects.waveEffect)

	for i = 1, configs.totalTiles do
		dmgCombat:execute(creature, Variant(area[i]))
	end
end

	-- Main callback
function onMainTargetTile(creature, tPos)
	area[tilesCount] = tPos
	if tilesCount == configs.totalTiles then
		performCombatEffects(creature)
	end
	tilesCount = tilesCount + 1
end

	-- Combat object
local combat = Combat()
combat:setParameter(COMBAT_PARAM_TYPE, COMBAT_PHYSICALDAMAGE)
combat:setArea(createCombatArea(AREA_BEAM5, AREADIAGONAL_BEAM5))
combat:setCallback(CALLBACK_PARAM_TARGETTILE, "onMainTargetTile")

-- Função de combate principal
local function doMainCombat(cid, tid)
	local creature = Creature(cid)
	local target = Creature(tid)

	if not target or not creature then return end

	local tPos = target:getPosition()
	local pos = creature:getPosition()
	direction = pos:getDirectionTo(tPos)

	tilesCount = 1
	combat:execute(creature, Variant(pos:getNextPosition(direction)))
end

-- Trigger Combat
	-- Função pra mandar efeito com delay
local function sendMagicEffectEvent(pos)
	pos:sendMagicEffect(configs.effects.hitEffect)
end

	-- Delayed hit effect
function onTriggerTargetTile(creature, tPos)
	hitTimer = inversionMap[direction] 
		and (configs.hitDelay * configs.totalTiles) - (configs.hitDelay * tilesCount)
		or configs.hitDelay * tilesCount
	addEvent(sendMagicEffectEvent, hitTimer, tPos)

	tilesCount = tilesCount + 1
end

	-- Combat Object
local trigger = Combat()
trigger:setArea(createCombatArea(AREA_BEAM5, AREADIAGONAL_BEAM5))
trigger:setCallback(CALLBACK_PARAM_TARGETTILE, "onTriggerTargetTile")

local spell = Spell("instant")

function spell.onCastSpell(creature, var)
	tilesCount = 1
	local tPos = Creature(var.number):getPosition()
	local pos = creature:getPosition()
	direction = pos:getDirectionTo(tPos)
	trigger:execute(creature, Variant(pos:getNextPosition(direction)))

	for wave = 1, configs.waveSize do
		addEvent(doMainCombat, configs.firstDelay + (wave * configs.waveDelay), creature:getId(), var.number)
	end
end

spell:name("Whirlwind Throw")
spell:words("exori arrows")
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
spell:needTarget(true)
spell:needLearn(false)
spell:blockWalls(true)
spell:register()
