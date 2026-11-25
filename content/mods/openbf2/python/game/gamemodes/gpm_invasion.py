# Invasion (OpenBF2 only, needs the engine's Invasion support)
#
# The defenders' control points form groups that the attackers take in order. Only the open group
# can be captured (both teams, so the defenders can retake its points). When the attackers hold
# every point of the open group, it locks for good and the next group opens. Taking the last group
# starts the final bleed: the defenders can no longer spawn and their tickets drain at
# FINAL_BLEED_PER_SECOND, so the attackers have that long to finish off the rest; the attackers win
# when the defenders' tickets reach 0. Running out of tickets or the time limit loses. Otherwise
# only the attackers lose tickets.
#
# Level data, all in the layer's GamePlayObjects.con (GameModes/gpm_invasion/<size>/):
# - ObjectTemplate.supplyGroupId <n> on each objective's control point template: its group, taken
#   in ascending order (1 first). Points without a group (the attackers' main base) never change.
# - ObjectTemplate.enemyTicketLossWhenCaptured <n> on an objective: tickets the attackers gain
#   when its group falls (summed over the group).
# - gameLogic.setDefaultNumberOfTicketsEx <size> <team> <n>: start tickets per team.
# - ObjectTemplate.setOnlyForTeam <team> on spawn points (engine): attackers' forward spawns and
#   the defenders' own spawns on an objective.
#
# Spawning follows the groups: the newest locked group is the attackers' forward spawn, the open
# group spawns defenders while they hold its points, the last group spawns defenders until it opens
# (then the open group's rule), and every other group's spawn points are switched off
# (cp_setParam 'spawnEnabled'). Vehicle spawners
# stay on: an objective's emplacements and vehicles stand from the round's start.

TAKEOVERTYPE_CAPTURE = 1
TAKEOVERTYPE_NEUTRALIZE = 2

SCORE_CAPTURE = 2
SCORE_NEUTRALIZE = 2
SCORE_CAPTUREASSIST = 1
SCORE_NEUTRALIZEASSIST = 1
SCORE_DEFEND = 1

VICTORY_TICKETS = 3

FINAL_BLEED_PER_SECOND = 999.0 / 60.0

Top = 0
Middle = 1
Bottom = 2

import host
import bf2
import math
from game.scoringCommon import addScore, RPL
from bf2 import g_debug

g_controlPoints = []
g_groups = []        # lists of control points, in attack order
g_open = 0           # index of the open group in g_groups
g_attackers = 2
g_defenders = 1



def init():
	host.registerGameStatusHandler(onGameStatusChanged)
	host.sh_setEnableCommander(1)
	host.registerHandler('TimeLimitReached', onTimeLimitReached, 1)
	if g_debug: print "gpm_invasion.py initialized"



def deinit():
	bf2.triggerManager.destroyAllTriggers()
	global g_controlPoints, g_groups
	g_controlPoints = []
	g_groups = []
	host.unregisterGameStatusHandler(onGameStatusChanged)
	if g_debug: print "gpm_invasion.py uninitialized"



def onGameStatusChanged(status):
	global g_controlPoints, g_groups, g_open, g_attackers, g_defenders
	if status != bf2.GameStatus.Playing:
		bf2.triggerManager.destroyAllTriggers()
		g_controlPoints = []
		g_groups = []
		return

	g_controlPoints = bf2.objectManager.getObjectsOfType('dice.hfe.world.ObjectTemplate.ControlPoint')
	byGroup = {}
	for obj in g_controlPoints:
		radius = float(obj.getTemplateProperty('radius'))
		if int(obj.cp_getParam('isHemisphere')) != 0:
			id = bf2.triggerManager.createHemiSphericalTrigger(obj, onCPTrigger, '<<PCO>>', radius, (1, 2, 3))
		else:
			id = bf2.triggerManager.createRadiusTrigger(obj, onCPTrigger, '<<PCO>>', radius, (1, 2, 3))
		obj.triggerId = id
		if obj.cp_getParam('team') > 0:
			obj.flagPosition = Top
		else:
			obj.flagPosition = Bottom
		obj.group = -1
		groupId = int(obj.cp_getParam('supplyGroupId'))
		if groupId > 0:
			byGroup.setdefault(groupId, []).append(obj)

	ids = byGroup.keys()
	ids.sort()
	g_groups = []
	for groupId in ids:
		for obj in byGroup[groupId]:
			obj.group = len(g_groups)
		g_groups.append(byGroup[groupId])
	g_open = 0
	if g_groups:
		g_defenders = g_groups[0][0].cp_getParam('team')
		if g_defenders not in (1, 2):
			g_defenders = 1
	g_attackers = 3 - g_defenders
	if not g_groups:
		print "gpm_invasion: no control point has a supplyGroupId, nothing to attack"

	host.registerHandler('ControlPointChangedOwner', onCPStatusChange)

	for team in (1, 2):
		tickets = calcStartTickets(bf2.gameLogic.getDefaultTickets(team))
		bf2.gameLogic.setTickets(team, tickets)
		bf2.gameLogic.setTicketState(team, 0)
		bf2.gameLogic.setTicketChangePerSecond(team, 0)
		bf2.gameLogic.setTicketLimit(team, 1, 0)
		bf2.gameLogic.setTicketLimit(team, 2, 10)
		bf2.gameLogic.setTicketLimit(team, 3, int(tickets * 0.1))
		bf2.gameLogic.setTicketLimit(team, 4, int(tickets * 0.2))
	host.registerHandler('TicketLimitReached', onTicketLimitReached)

	host.registerHandler('PlayerDeath', onPlayerDeath)
	host.registerHandler('PlayerKilled', onPlayerKilled)
	host.registerHandler('PlayerRevived', onPlayerRevived)
	host.registerHandler('PlayerSpawn', onPlayerSpawn)
	host.registerHandler('EnterVehicle', onEnterVehicle)
	host.registerHandler('ExitVehicle', onExitVehicle)

	applyGroupState()
	if g_debug: print "Invasion gamemode initialized, %d groups, attackers %d" % (len(g_groups), g_attackers)



# The layer's own ticket counts; the server's ticket ratio applies only to multiplayer.
def calcStartTickets(mapDefaultTickets):
	if host.sgl_getIsAIGame() == 1:
		return mapDefaultTickets
	return int(mapDefaultTickets * (bf2.serverSettings.getTicketRatio() / 100.0))



# ---- groups ----

def isCapturable(cp):
	return cp.cp_getParam('allowCaptureByTeam', 1) or cp.cp_getParam('allowCaptureByTeam', 2)


def applyGroupState():
	last = len(g_groups) - 1
	for cp in g_controlPoints:
		g = cp.group
		if g < 0:
			continue
		allow = g == g_open
		cp.cp_setParam('allowCaptureByTeam', 1, int(allow))
		cp.cp_setParam('allowCaptureByTeam', 2, int(allow))
		if not allow:
			freezeFlag(cp)
		if g < g_open:
			spawn = g == g_open - 1
		elif g == g_open:
			spawn = cp.cp_getParam('team') == g_defenders
		else:
			spawn = g == last
		cp.cp_setParam('spawnEnabled', int(spawn))


# A point that stops being capturable keeps the flag speed onCPTrigger last set (onCPTrigger ignores
# it from then on): a flag the defenders were pulling down when its group locked went on sinking and
# left the point neutral for good. Stop it, and raise an owned point's flag back to the top.
def freezeFlag(cp):
	if cp.cp_getParam('team') != 0 and cp.flagPosition != Top:
		cp.cp_setParam('takeOverChangePerSecond', 0.5)
	else:
		cp.cp_setParam('takeOverChangePerSecond', 0.0)


def checkOpenGroup():
	global g_open
	if g_open >= len(g_groups):
		return
	for cp in g_groups[g_open]:
		if cp.cp_getParam('team') != g_attackers:
			applyGroupState()
			return

	bonus = 0
	for cp in g_groups[g_open]:
		bonus += int(cp.cp_getParam('enemyTicketLossWhenCaptured'))
	g_open += 1
	if bonus > 0:
		bf2.gameLogic.setTickets(g_attackers, bf2.gameLogic.getTickets(g_attackers) + bonus)
	if g_open >= len(g_groups):
		bf2.gameLogic.setTicketChangePerSecond(g_defenders, -FINAL_BLEED_PER_SECOND)
	applyGroupState()



# ---- round end ----

def onTimeLimitReached(value):
	host.sgl_endGame(g_defenders, VICTORY_TICKETS)


def onTicketLimitReached(team, limitId):
	if limitId == -1:
		bf2.gameLogic.setTicketState(1, 0)
		bf2.gameLogic.setTicketState(2, 0)
		host.sgl_endGame(3 - team, VICTORY_TICKETS)
	else:
		updateTicketWarning(team, limitId)


def updateTicketWarning(team, limitId):
	oldTicketState = bf2.gameLogic.getTicketState(team)
	newTicketState = 0
	if oldTicketState >= 10:
		newTicketState = 10
	if limitId == -2:
		newTicketState = 10
	elif limitId == 2:
		newTicketState = 0
	elif limitId == -3:
		newTicketState += 2
	elif limitId == -4:
		newTicketState += 1
	if oldTicketState != newTicketState:
		bf2.gameLogic.setTicketState(team, newTicketState)



# ---- capturing (conquest's rules, limited to the open group) ----

def onCPTrigger(triggerId, cp, vehicle, enter, userData):
	if not cp.isValid(): return
	if vehicle and vehicle.getParent(): return
	if cp.cp_getParam('unableToChangeTeam') != 0 or not isCapturable(cp):
		return

	playersInVehicle = None
	if vehicle:
		playersInVehicle = vehicle.getOccupyingPlayers()

	if enter:
		for p in playersInVehicle:
			occupied = getOccupyingCP(p)
			if occupied != None and not p.getIsInsideCP():
				p.enterCpAt = host.timer_getWallTime()

	if vehicle:
		for p in playersInVehicle:
			if p == playersInVehicle[0]:
				p.setIsInsideCP(enter)
			else:
				p.setIsInsideCP(0)
				if enter == 1:
					bf2.gameLogic.sendHudEvent(p, 66, 49) #66 = HEEnableHelpMessage, 49 = VHMExitToCaptureFlag

	team1Occupants = 0
	team2Occupants = 0
	for o in bf2.triggerManager.getObjects(cp.triggerId):
		if not o: continue
		if o.getParent(): continue
		occupyingPlayers = o.getOccupyingPlayers()
		for p in occupyingPlayers:
			if p != occupyingPlayers[0]:
				continue
			if p.isAlive() and not p.isManDown() and not p.killed:
				if p.getTeam() == 1:
					team1Occupants += 1
				elif p.getTeam() == 2:
					team2Occupants += 1

	team1OverWeight = team1Occupants - team2Occupants
	if team1OverWeight > 0:
		attackingTeam = 1
	elif team1OverWeight < 0:
		attackingTeam = 2
	else:
		attackingTeam = 0

	if team1Occupants == 0 and team2Occupants == 0:
		if cp.cp_getParam('team') == 0:
			attackOverWeight = -0.5
		else:
			attackOverWeight = 0.5
		timeToChangeControl = cp.cp_getParam('timeToLoseControl')
	elif cp.cp_getParam('flag') == attackingTeam or (cp.flagPosition == Bottom and cp.cp_getParam('team') == 0):
		attackOverWeight = abs(team1OverWeight)
		timeToChangeControl = cp.cp_getParam('timeToGetControl')
	else:
		attackOverWeight = - abs(team1OverWeight)
		timeToChangeControl = cp.cp_getParam('timeToLoseControl')

	if cp.cp_getParam('onlyTakeableByTeam') != 0 and cp.cp_getParam('onlyTakeableByTeam') != attackingTeam:
		return
	if attackingTeam != 0 and not cp.cp_getParam('allowCaptureByTeam', attackingTeam):
		return

	if cp.flagPosition == Bottom:
		cp.cp_setParam('flag', attackingTeam)

	if timeToChangeControl > 0:
		takeOverChangePerSecond = 1.0 * attackOverWeight / timeToChangeControl
	else:
		takeOverChangePerSecond = 0.0
	if (cp.flagPosition == Top and takeOverChangePerSecond > 0) or (cp.flagPosition == Bottom and takeOverChangePerSecond < 0):
		takeOverChangePerSecond = 0.0
	if abs(takeOverChangePerSecond) > 0:
		cp.flagPosition = Middle
	cp.cp_setParam('takeOverChangePerSecond', takeOverChangePerSecond)



def onCPStatusChange(cp, top):
	playerId = -1
	takeoverType = -1
	newTeam = -1
	scoringTeam = -1

	if top:	cp.flagPosition = Top
	else:   cp.flagPosition = Bottom

	if cp.group >= 0 and not isCapturable(cp):
		freezeFlag(cp)
		return

	if cp.cp_getParam('team') != 0:
		if not top:
			newTeam = 0
			scoringTeam = 3 - cp.cp_getParam('team')
			takeoverType = TAKEOVERTYPE_NEUTRALIZE
	elif top:
		newTeam = cp.cp_getParam('flag')
		scoringTeam = newTeam
		takeoverType = TAKEOVERTYPE_CAPTURE

	if takeoverType > 0:
		scoringPlayers = []
		firstPlayers = []
		for o in bf2.triggerManager.getObjects(cp.triggerId):
			if not o or o.getParent(): continue
			occupyingPlayers = o.getOccupyingPlayers()
			for p in occupyingPlayers:
				if p != occupyingPlayers[0]:
					continue
				if p.isAlive() and not p.isManDown() and p.getTeam() == scoringTeam:
					if len(firstPlayers) == 0 or p.enterCpAt < firstPlayers[0].enterCpAt:
						firstPlayers = [p]
					elif p.enterCpAt == firstPlayers[0].enterCpAt:
						firstPlayers += [p]
					if not p in scoringPlayers:
						scoringPlayers += [p]

		for p in scoringPlayers:
			if takeoverType == TAKEOVERTYPE_CAPTURE:
				if p in firstPlayers:
					p.score.cpCaptures += 1
					addScore(p, SCORE_CAPTURE, RPL)
					bf2.gameLogic.sendGameEvent(p, 12, 0) #12 = Conquest, 0 = Capture
					playerId = p.index
				else:
					p.score.cpAssists += 1
					addScore(p, SCORE_CAPTUREASSIST, RPL)
					bf2.gameLogic.sendGameEvent(p, 12, 2) #12 = Conquest, 2 = Assist
			else:
				if p in firstPlayers:
					p.score.cpNeutralizes += 1
					addScore(p, SCORE_NEUTRALIZE, RPL)
					bf2.gameLogic.sendGameEvent(p, 12, 3) #12 = Conquest, 3 = Neutralize
				else:
					p.score.cpNeutralizeAssists += 1
					addScore(p, SCORE_NEUTRALIZEASSIST, RPL)
					bf2.gameLogic.sendGameEvent(p, 12, 4) #12 = Conquest, 4 = Neutralize assist

	cp.cp_setParam('playerId', playerId) #always set player first
	if newTeam != -1 and cp.cp_getParam('team') != newTeam:
		cp.cp_setParam('team', newTeam)
	onCPTrigger(cp.triggerId, cp, 0, 0, 0)
	if cp.group == g_open:
		checkOpenGroup()



# ---- players ----

# Only the attackers pay for deaths; the defenders' tickets never run down.
def onPlayerDeath(victim, vehicle):
	if victim != None and victim.getTeam() == g_attackers:
		bf2.gameLogic.setTickets(g_attackers, bf2.gameLogic.getTickets(g_attackers) - 1)


def onPlayerKilled(victim, attacker, weapon, assists, object):
	if not victim:
		return
	victim.killed = True
	cp = getOccupyingCP(victim)
	if cp != None:
		onCPTrigger(-1, cp, victim.getVehicle(), False, None)
		if attacker != None and attacker.getTeam() != victim.getTeam() and isCapturable(cp)\
		   and cp.cp_getParam('unableToChangeTeam') == 0 and cp.cp_getParam('team') == attacker.getTeam():
			attacker.score.cpDefends += 1
			addScore(attacker, SCORE_DEFEND, RPL)
			bf2.gameLogic.sendGameEvent(attacker, 12, 1) #12 = Conquest, 1 = Defend


def onPlayerRevived(victim, attacker):
	victim.killed = False
	cp = getOccupyingCP(victim)
	if cp != None:
		onCPTrigger(-1, cp, victim.getVehicle(), True, None)


def onPlayerSpawn(player, soldier):
	player.killed = False


def onEnterVehicle(player, vehicle, freeSoldier = False):
	player.setIsInsideCP(False)
	cp = getOccupyingCP(player)
	if cp != None:
		onCPTrigger(-1, cp, vehicle, False, None)
		updateCaptureStatus(vehicle)


def onExitVehicle(player, vehicle):
	cp = getOccupyingCP(player)
	player.setIsInsideCP(cp != None and cp.cp_getParam('unableToChangeTeam') == 0 and isCapturable(cp))
	updateCaptureStatus(vehicle)


def updateCaptureStatus(vehicle):
	rootVehicle = bf2.objectManager.getRootParent(vehicle)
	playersInVehicle = rootVehicle.getOccupyingPlayers()
	for p in playersInVehicle:
		cp = getOccupyingCP(p)
		p.setIsInsideCP(cp != None and cp.cp_getParam('unableToChangeTeam') == 0 and isCapturable(cp) and p == playersInVehicle[0])


def getOccupyingCP(player):
	vehicle = player.getVehicle()
	playerPos = vehicle.getPosition()
	closestCP = None
	if len(g_controlPoints) == 0: return None
	for obj in g_controlPoints:
		distanceTo = getVectorDistance(playerPos, obj.getPosition())
		if closestCP == None or distanceTo < closestCPdist:
			closestCP = obj
			closestCPdist = distanceTo
	for o in bf2.triggerManager.getObjects(closestCP.triggerId):
		if o == player.getDefaultVehicle():
			return closestCP
		for p in o.getOccupyingPlayers():
			if p == player:
				return closestCP
	return None


def getVectorDistance(pos1, pos2):
	dx = pos1[0] - pos2[0]
	dy = pos1[1] - pos2[1]
	dz = pos1[2] - pos2[2]
	return math.sqrt(dx * dx + dy * dy + dz * dz)
