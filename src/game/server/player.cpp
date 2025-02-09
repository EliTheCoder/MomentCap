/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "player.h"
#include <engine/shared/config.h>
#include <new>

#include "gamecontext.h"
#include "gamemodes/DDRace.h"
#include <engine/server.h>
#include <game/gamecore.h>
#include <game/server/teams.h>
#include <game/version.h>
#include <time.h>

#include <game/server/posinghelper.h>

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

void CPlayer::Pose()
{
	if(g_Config.m_SvCaptureInterval < 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), g_Config.m_TrEventOver);
		GameServer()->SendChatTarget(GetCID(), aBuf);
		return;
	}

	int JoinedTime = (Server()->Tick() - m_JoinTick) / Server()->TickSpeed();
	if(JoinedTime < g_Config.m_SvCaptureDelay)
	{
		int TimeLeft = g_Config.m_SvCaptureDelay - JoinedTime;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), g_Config.m_TrDelay, TimeLeft);
		GameServer()->SendChatTarget(GetCID(), aBuf);
		return;
	}

	int TimePassed = (Server()->Tick() - m_LastPoseTick) / Server()->TickSpeed();
	if(TimePassed < 1)
		return;

	if(CPoseCharacter::HasPose(this))
	{
		if(CPoseCharacter::RemovePose(this))
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN, GetCID());
		else
			GameServer()->CreateSoundGlobal(SOUND_WEAPON_NOAMMO, GetCID());
	}
	else
	{
		int TimePassed = (Server()->Tick() - m_LastKill) / Server()->TickSpeed();
		if(TimePassed < g_Config.m_SvCaptureInterval)
		{
			int TimeLeft = g_Config.m_SvCaptureInterval - TimePassed;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), g_Config.m_TrWait, TimeLeft);
			GameServer()->SendChatTarget(GetCID(), aBuf);
			return;
		}
		if(CPoseCharacter::Pose(this))
			GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, GetCID());
		else
			GameServer()->CreateSoundGlobal(SOUND_WEAPON_NOAMMO, GetCID());

		m_LastKill = Server()->Tick();
	}

	m_LastPoseTick = Server()->Tick();
	m_LastBrTick = 0;
	m_LastPoseSnapTick = 0;
}

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	m_NumInputs = 0;
	Reset();
	GameServer()->Antibot()->OnPlayerInit(m_ClientID);
	for(auto &ID : m_GhostSnapIDs)
		ID = Server()->SnapNewID();
}

CPlayer::~CPlayer()
{
	GameServer()->Antibot()->OnPlayerDestroy(m_ClientID);
	delete m_pLastTarget;
	delete m_pCharacter;
	m_pCharacter = 0;
	for(auto ID : m_GhostSnapIDs)
		Server()->SnapFreeID(ID);
}

void CPlayer::Reset()
{
	mem_zero(m_LastSnapped, sizeof(m_LastSnapped));
	mem_zero(m_LastRealToFake, sizeof(m_LastRealToFake));
	for(int i = 1; i < FAKE_MAX_CLIENTS; i++)
		m_FakeIDPool.push_back(i);

	m_ForcingViewPos = 0;
	m_ForcedViewPos = vec2(0, 0);

	m_PoseOnScreen = 0;
	m_LastPoseCommand = 0;
	m_LastBrTick = Server()->Tick() + 4;
	m_LastPoseTick = 0;
	m_DieTick = Server()->Tick();
	m_PreviousDieTick = m_DieTick;
	m_JoinTick = Server()->Tick();
	delete m_pCharacter;
	m_pCharacter = 0;
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	m_LastInvited = 0;
	m_SendReal = false;
	m_WeakHookSpawn = false;

	int *pIdMap = Server()->GetIdMap(m_ClientID);
	for(int i = 1; i < VANILLA_MAX_CLIENTS; i++)
	{
		pIdMap[i] = -1;
	}
	pIdMap[0] = m_ClientID;

	// DDRace

	m_LastCommandPos = 0;
	m_LastPlaytime = 0;
	m_Sent1stAfkWarning = 0;
	m_Sent2ndAfkWarning = 0;
	m_ChatScore = 0;
	m_Moderating = false;
	m_EyeEmote = true;
	if(Server()->IsSixup(m_ClientID))
		m_TimerType = TIMERTYPE_SIXUP;
	else
		m_TimerType = (g_Config.m_SvDefaultTimerType == TIMERTYPE_GAMETIMER || g_Config.m_SvDefaultTimerType == TIMERTYPE_GAMETIMER_AND_BROADCAST) ? TIMERTYPE_BROADCAST : g_Config.m_SvDefaultTimerType;

	m_DefEmote = EMOTE_NORMAL;
	m_Afk = true;
	m_LastWhisperTo = -1;
	m_LastSetSpectatorMode = 0;
	m_TimeoutCode[0] = '\0';
	delete m_pLastTarget;
	m_pLastTarget = nullptr;
	m_TuneZone = 0;
	m_TuneZoneOld = m_TuneZone;
	m_Halloween = false;
	m_FirstPacket = true;

	m_SendVoteIndex = -1;

	if(g_Config.m_Events)
	{
		time_t rawtime;
		struct tm *timeinfo;
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		if((timeinfo->tm_mon == 11 && timeinfo->tm_mday == 31) || (timeinfo->tm_mon == 0 && timeinfo->tm_mday == 1))
		{ // New Year
			m_DefEmote = EMOTE_HAPPY;
		}
		else if((timeinfo->tm_mon == 9 && timeinfo->tm_mday == 31) || (timeinfo->tm_mon == 10 && timeinfo->tm_mday == 1))
		{ // Halloween
			m_DefEmote = EMOTE_ANGRY;
			m_Halloween = true;
		}
		else
		{
			m_DefEmote = EMOTE_NORMAL;
		}
	}
	m_DefEmoteReset = -1;

	// GameServer()->Score()->PlayerData(m_ClientID)->Reset();

	m_ShowOthers = true; // g_Config.m_SvShowOthersDefault;

	// HACK: safe no show all
	m_ShowAll = false; // g_Config.m_SvShowAllDefault;

	m_ShowDistance = vec2(1000, 800);
	m_SpecTeam = 0;
	m_NinjaJetpack = false;

	m_Paused = PAUSE_NONE;
	m_DND = false;

	m_LastPause = 0;
	m_Score = -9999;
	m_HasFinishScore = false;

	// Variable initialized:
	m_Last_Team = 0;
	m_LastSQLQuery = 0;
	m_ScoreQueryResult = nullptr;
	m_ScoreFinishResult = nullptr;

	int64 Now = Server()->Tick();
	int64 TickSpeed = Server()->TickSpeed();
	// If the player joins within ten seconds of the server becoming
	// non-empty, allow them to vote immediately. This allows players to
	// vote after map changes or when they join an empty server.
	//
	// Otherwise, block voting in the beginning after joining.
	if(Now > GameServer()->m_NonEmptySince + 10 * TickSpeed)
		m_FirstVoteTick = Now + g_Config.m_SvJoinVoteDelay * TickSpeed;
	else
		m_FirstVoteTick = Now;

	m_NotEligibleForFinish = false;
	m_EligibleForFinishCheck = 0;
	m_VotedForPractice = false;
}

static int PlayerFlags_SevenToSix(int Flags)
{
	int Six = 0;
	if(Flags & protocol7::PLAYERFLAG_CHATTING)
		Six |= PLAYERFLAG_CHATTING;
	if(Flags & protocol7::PLAYERFLAG_SCOREBOARD)
		Six |= PLAYERFLAG_SCOREBOARD;

	return Six;
}

static int PlayerFlags_SixToSeven(int Flags)
{
	int Seven = 0;
	if(Flags & PLAYERFLAG_CHATTING)
		Seven |= protocol7::PLAYERFLAG_CHATTING;
	if(Flags & PLAYERFLAG_SCOREBOARD)
		Seven |= protocol7::PLAYERFLAG_SCOREBOARD;

	return Seven;
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < Server()->MaxClients() - g_Config.m_DbgDummies)
#endif
		// 	if(m_ScoreQueryResult != nullptr && m_ScoreQueryResult->m_Completed)
		// 	{
		// 		ProcessScoreResult(*m_ScoreQueryResult);
		// 		m_ScoreQueryResult = nullptr;
		// 	}
		// if(m_ScoreFinishResult != nullptr && m_ScoreFinishResult->m_Completed)
		// {
		// 	ProcessScoreResult(*m_ScoreFinishResult);
		// 	m_ScoreFinishResult = nullptr;
		// }

		if(!Server()->ClientIngame(m_ClientID))
			return;

	if(m_ChatScore > 0)
		m_ChatScore--;

	Server()->SetClientScore(m_ClientID, m_Score);

	if(m_Moderating && m_Afk)
	{
		m_Moderating = false;
		GameServer()->SendChatTarget(m_ClientID, "Active moderator mode disabled because you are afk.");

		if(!GameServer()->PlayerModerating())
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Server kick/spec votes are no longer actively moderated.");
	}

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = maximum(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = minimum(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum / Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(Server()->GetNetErrorString(m_ClientID)[0])
	{
		m_Afk = true;

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' would have timed out, but can use timeout protection now", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
		Server()->ResetNetErrorString(m_ClientID);
	}

	// HACK: broadcasting
	int JoinedTime = (Server()->Tick() - m_JoinTick) / Server()->TickSpeed();

	if(Server()->ClientAuthed(m_ClientID) && (m_Paused || m_Team == TEAM_SPECTATORS))
	{
		// Auth Tool

		char aBuf[256] = {0};
		int Offset = 0;

		Offset += str_format(aBuf, sizeof(aBuf), "%d\n", m_PoseOnScreen);

		CCharacter *pChar = GameServer()->m_World.ClosestCharacter(m_ViewPos, 200.0f, NULL);
		CPlayer *pPlayer = NULL;
		if(pChar)
		{
			char aAddr[NETADDR_MAXSTRSIZE] = {0};
			pPlayer = pChar->GetPlayer();
			Server()->GetClientAddr(pPlayer->GetCID(), aAddr, NETADDR_MAXSTRSIZE);
			Offset += str_format(aBuf + Offset, sizeof(aBuf) - Offset - 1, "--Player--\nName(%d): %s\nIP: %s\n\n", pPlayer->GetCID(), Server()->ClientName(pPlayer->GetCID()), aAddr);
		}

		const CPoseCharacter *pPoseCharacter = CPoseCharacter::ClosestPose(m_ViewPos, 200.0f);
		if(pPoseCharacter)
		{
			char aName[MAX_NAME_LENGTH] = {0};
			int aInts[4] = {0};
			aInts[0] = pPoseCharacter->m_ClientInfo.m_Name0;
			aInts[1] = pPoseCharacter->m_ClientInfo.m_Name1;
			aInts[2] = pPoseCharacter->m_ClientInfo.m_Name2;
			aInts[3] = pPoseCharacter->m_ClientInfo.m_Name3;
			IntsToStr(aInts, 4, aName);

			str_format(aBuf + Offset, sizeof(aBuf) - Offset - 1, "--Capture--\n%s\nIP: %s\nTimeout: %s", aName, pPoseCharacter->m_aAddr, pPoseCharacter->m_aTimeoutCode);
		}

		if(aBuf[0] != 0)
			GameServer()->SendBroadcast(aBuf, m_ClientID, Server()->ClientAuthed(m_ClientID));
	}
	else if((Server()->Tick() - m_LastBrTick) / Server()->TickSpeed() > 5 && g_Config.m_SvCaptureInterval >= 0)
	{
		char aBuf[256];
		bool HasPose = CPoseCharacter::HasPose(this);
		bool CanCapture = JoinedTime > g_Config.m_SvCaptureDelay;

		str_format(aBuf, sizeof(aBuf), "%s", CanCapture ? (HasPose ? g_Config.m_TrCancelPrompt : g_Config.m_TrCapturePrompt) : g_Config.m_TrBroadcast);

		GameServer()->SendBroadcast(aBuf, m_ClientID, false);
		m_LastBrTick = Server()->Tick();
	}

	if(!GameServer()->m_World.m_Paused)
	{
		int EarliestRespawnTick = m_PreviousDieTick + Server()->TickSpeed() * 3;
		int RespawnTick = maximum(m_DieTick, EarliestRespawnTick) + 2;
		if(!m_pCharacter && RespawnTick <= Server()->Tick())
			m_Spawning = true;

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				ProcessPause();
				if(!m_Paused)
					m_ViewPos = m_pCharacter->m_Pos;
			}
			else if(!m_pCharacter->IsPaused())
			{
				delete m_pCharacter;
				m_pCharacter = 0;
			}
		}
		else if(m_Spawning && !m_WeakHookSpawn)
			TryRespawn();
	}
	else
	{
		++m_DieTick;
		++m_PreviousDieTick;
		++m_JoinTick;
		++m_LastActionTick;
		++m_TeamChangeTick;
	}

	m_TuneZoneOld = m_TuneZone; // determine needed tunings with viewpos
	int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_ViewPos);
	m_TuneZone = GameServer()->Collision()->IsTune(CurrentIndex);

	if(m_TuneZone != m_TuneZoneOld) // don't send tunings all the time
	{
		GameServer()->SendTuningParams(m_ClientID, m_TuneZone);
	}
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if((m_Team == TEAM_SPECTATORS || m_Paused) && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID] && GameServer()->m_apPlayers[m_SpectatorID]->GetCharacter())
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->GetCharacter()->m_Pos;
}

void CPlayer::PostPostTick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < Server()->MaxClients() - g_Config.m_DbgDummies)
#endif
		if(!Server()->ClientIngame(m_ClientID))
			return;

	if(!GameServer()->m_World.m_Paused && !m_pCharacter && m_Spawning && m_WeakHookSpawn)
		TryRespawn();
}

void CPlayer::SnapGhost(int SnappingClient)
{
	if(!g_Config.m_SvShowClients)
		return;

	if(GetCID() == SnappingClient)
		return;

	if(!GetCharacter())
		return;

	CCharacter *pChar = GetCharacter();
	if(pChar->NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_GhostSnapIDs[0], sizeof(CNetObj_Pickup)));
	if(!pObj)
		return;

	pObj->m_X = (int)pChar->m_Pos.x;
	pObj->m_Y = (int)pChar->m_Pos.y;

	if(pChar->m_FreezeTime)
	{
		pObj->m_Type = POWERUP_NINJA;
		pObj->m_Subtype = POWERUP_NINJA;
	}
	else
	{
		pObj->m_Type = POWERUP_WEAPON;
		pObj->m_Subtype = pChar->Core()->m_ActiveWeapon;
	}

	// pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_GhostSnapIDs[1], sizeof(CNetObj_Laser)));
	// if(!pObj)
	// 	return;

	// pObj->m_X = (int)pChar->m_Pos.x + 12;
	// pObj->m_Y = (int)pChar->m_Pos.y + 12;
	// pObj->m_FromX = pObj->m_X - 24;
	// pObj->m_FromY = pObj->m_Y - 24;
	// pObj->m_StartTick = Server()->Tick() - 4;

	if(pChar->Core()->m_HookState > HOOK_IDLE)
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_GhostSnapIDs[1], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)pChar->m_Pos.x;
		pObj->m_Y = (int)pChar->m_Pos.y;
		pObj->m_FromX = (int)pChar->Core()->m_HookPos.x;
		pObj->m_FromY = (int)pChar->Core()->m_HookPos.y;
		pObj->m_StartTick = Server()->Tick() - 4;
	}

	if(m_PlayerFlags & PLAYERFLAG_CHATTING)
	{
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_GhostSnapIDs[2], sizeof(CNetObj_Projectile)));
		if(!pObj)
			return;

		pObj->m_X = (int)pChar->m_Pos.x + 18;
		pObj->m_Y = (int)pChar->m_Pos.y - 24;
		pObj->m_Type = WEAPON_GRENADE;
		pObj->m_VelX = 0;
		pObj->m_VelY = 0;
		pObj->m_StartTick = Server()->Tick() - 4;
	}
}

void CPlayer::Snap(int SnappingClient, int FakeID)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < Server()->MaxClients() - g_Config.m_DbgDummies)
#endif
		if(!Server()->ClientIngame(m_ClientID))
			return;

	int id = m_ClientID;
	if(SnappingClient > -1 && !Server()->Translate(id, SnappingClient))
		return;

	if(FakeID < 0 || FakeID >= FAKE_MAX_CLIENTS)
		return;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, FakeID, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_SkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	int ClientVersion = GetClientVersion();
	int Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	int Score = abs(m_Score) * -1;

	// send 0 if times of others are not shown
	if(SnappingClient != m_ClientID && g_Config.m_SvHideScore)
		Score = -9999;

	// HACK: your score is always 0
	if(FakeID == 0)
		Score = 0;
	else
		Score = Server()->ClientAuthed(SnappingClient) ? -m_ClientID : -1;

	if(SnappingClient < 0 || !Server()->IsSixup(SnappingClient))
	{
		CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, FakeID, sizeof(CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_Latency = Latency;
		int minute = g_Config.m_SvYear / 100;
		int second = g_Config.m_SvYear % 100;
		pPlayerInfo->m_Score = -(minute * 60 + second);
		pPlayerInfo->m_Local = (FakeID == 0) && (m_Paused != PAUSE_PAUSED || ClientVersion >= VERSION_DDNET_OLD);
		pPlayerInfo->m_ClientID = FakeID;
		pPlayerInfo->m_Team = (ClientVersion < VERSION_DDNET_OLD || m_Paused != PAUSE_PAUSED || m_ClientID != SnappingClient) && m_Paused < PAUSE_SPEC ? m_Team : TEAM_SPECTATORS;

		if(m_ClientID == SnappingClient && m_Paused == PAUSE_PAUSED && ClientVersion < VERSION_DDNET_OLD)
			pPlayerInfo->m_Team = TEAM_SPECTATORS;
	}
	else
	{
		protocol7::CNetObj_PlayerInfo *pPlayerInfo = static_cast<protocol7::CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, FakeID, sizeof(protocol7::CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_PlayerFlags = PlayerFlags_SixToSeven(m_PlayerFlags);
		if(Server()->ClientAuthed(m_ClientID))
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_ADMIN;

		// Times are in milliseconds for 0.7
		pPlayerInfo->m_Score = Score == -9999 ? -1 : -Score * 1000;
		pPlayerInfo->m_Latency = Latency;
	}

	if(m_ClientID == SnappingClient && (m_Team == TEAM_SPECTATORS || m_Paused))
	{
		int SpecID = m_SpectatorID;
		// HACK: force spec follow
		if(m_ForcingViewPos > 0)
		{
			m_ForcingViewPos--;
			m_ViewPos = m_ForcedViewPos;
			SpecID = 0;
		}

		if(SnappingClient < 0 || !Server()->IsSixup(SnappingClient))
		{
			CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, FakeID, sizeof(CNetObj_SpectatorInfo)));
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpectatorID = SpecID;
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;

			if(pSpectatorInfo->m_SpectatorID != SPEC_FREEVIEW)
			{
				pSpectatorInfo->m_SpectatorID = 0;
			}
		}
		else
		{
			protocol7::CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<protocol7::CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, FakeID, sizeof(protocol7::CNetObj_SpectatorInfo)));
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpecMode = SpecID == SPEC_FREEVIEW ? protocol7::SPEC_FREEVIEW : protocol7::SPEC_PLAYER;
			pSpectatorInfo->m_SpectatorID = SpecID;
			if(pSpectatorInfo->m_SpectatorID != SPEC_FREEVIEW)
			{
				pSpectatorInfo->m_SpectatorID = 0;
			}
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;
		}
	}

	CNetObj_DDNetPlayer *pDDNetPlayer = static_cast<CNetObj_DDNetPlayer *>(Server()->SnapNewItem(NETOBJTYPE_DDNETPLAYER, FakeID, sizeof(CNetObj_DDNetPlayer)));
	if(!pDDNetPlayer)
		return;

	pDDNetPlayer->m_AuthLevel = Server()->GetAuthedState(id);
	pDDNetPlayer->m_Flags = 0;
	if(m_Afk)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
	if(m_Paused == PAUSE_SPEC)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_SPEC;
	if(m_Paused == PAUSE_PAUSED)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_PAUSED;

	if(SnappingClient >= 0 && Server()->IsSixup(SnappingClient) && m_pCharacter && m_pCharacter->m_DDRaceState == DDRACE_STARTED &&
		GameServer()->m_apPlayers[SnappingClient]->m_TimerType == TIMERTYPE_SIXUP)
	{
		protocol7::CNetObj_PlayerInfoRace *pRaceInfo = static_cast<protocol7::CNetObj_PlayerInfoRace *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_PLAYERINFORACE, FakeID, sizeof(protocol7::CNetObj_PlayerInfoRace)));
		pRaceInfo->m_RaceStartTick = m_pCharacter->m_StartTime;
	}

	// bool ShowSpec = m_pCharacter && m_pCharacter->IsPaused();

	// if(SnappingClient >= 0)
	// {
	// 	CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];
	// 	ShowSpec = ShowSpec && (GameServer()->GetDDRaceTeam(id) == GameServer()->GetDDRaceTeam(SnappingClient) || pSnapPlayer->m_ShowOthers == 1 || (pSnapPlayer->GetTeam() == TEAM_SPECTATORS || pSnapPlayer->IsPaused()));
	// }

	// if(ShowSpec)
	// {
	// 	CNetObj_SpecChar *pSpecChar = static_cast<CNetObj_SpecChar *>(Server()->SnapNewItem(NETOBJTYPE_SPECCHAR, FakeID, sizeof(CNetObj_SpecChar)));
	// 	pSpecChar->m_X = m_pCharacter->Core()->m_Pos.x;
	// 	pSpecChar->m_Y = m_pCharacter->Core()->m_Pos.y;
	// }
}

void CPlayer::FakeSnap()
{
	if(GetClientVersion() >= VERSION_DDNET_OLD)
		return;

	if(Server()->IsSixup(m_ClientID))
		return;

	int FakeID = VANILLA_MAX_CLIENTS - 1;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, FakeID, sizeof(CNetObj_ClientInfo)));

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, " ");
	StrToInts(&pClientInfo->m_Clan0, 3, "");
	StrToInts(&pClientInfo->m_Skin0, 6, "default");

	if(m_Paused != PAUSE_PAUSED)
		return;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, FakeID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = m_Latency.m_Min;
	pPlayerInfo->m_Local = 1;
	pPlayerInfo->m_ClientID = FakeID;
	pPlayerInfo->m_Score = -9999;
	pPlayerInfo->m_Team = TEAM_SPECTATORS;

	CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, FakeID, sizeof(CNetObj_SpectatorInfo)));
	if(!pSpectatorInfo)
		return;

	pSpectatorInfo->m_SpectatorID = m_SpectatorID;
	pSpectatorInfo->m_X = m_ViewPos.x;
	pSpectatorInfo->m_Y = m_ViewPos.y;
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(m_ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(m_ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

		bool WasModerator = m_Moderating;

		// Set this to false, otherwise PlayerModerating() will return true.
		m_Moderating = false;

		if(!GameServer()->PlayerModerating() && WasModerator)
			GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Server kick/spec votes are no longer actively moderated.");
	}

	CGameControllerDDRace *Controller = (CGameControllerDDRace *)GameServer()->m_pController;
	if(g_Config.m_SvTeam != 3)
		Controller->m_Teams.SetForceCharacterTeam(m_ClientID, TEAM_FLOCK);
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags & PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		return;

	AfkVoteTimer(NewInput);

	m_NumInputs++;

	if(m_pCharacter && !m_Paused)
		m_pCharacter->OnPredictedInput(NewInput);

	// Magic number when we can hope that client has successfully identified itself
	if(m_NumInputs == 20 && g_Config.m_SvClientSuggestion[0] != '\0' && GetClientVersion() <= VERSION_DDNET_OLD)
		GameServer()->SendBroadcast(g_Config.m_SvClientSuggestion, m_ClientID);
	else if(m_NumInputs == 200 && Server()->IsSixup(m_ClientID))
		GameServer()->SendBroadcast("This server uses an experimental translation from Teeworlds 0.7 to 0.6. Please report bugs on ddnet.tw/discord", m_ClientID);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(Server()->IsSixup(m_ClientID))
		NewInput->m_PlayerFlags = PlayerFlags_SevenToSix(NewInput->m_PlayerFlags);

	if(NewInput->m_PlayerFlags)
		Server()->SetClientFlags(m_ClientID, NewInput->m_PlayerFlags);

	if(AfkTimer(NewInput->m_TargetX, NewInput->m_TargetY))
		return; // we must return if kicked, as player struct is already deleted
	AfkVoteTimer(NewInput);

	if(((!m_pCharacter && m_Team == TEAM_SPECTATORS) || m_Paused) && m_SpectatorID == SPEC_FREEVIEW)
		m_ViewPos = vec2(NewInput->m_TargetX, NewInput->m_TargetY);

	if(NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags & PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter && m_Paused)
		m_pCharacter->ResetInput();

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire & 1))
		m_Spawning = true;

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire & 1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
	}
}

void CPlayer::OnPredictedEarlyInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags & PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter && !m_Paused)
		m_pCharacter->OnDirectInput(NewInput);
}

int CPlayer::GetClientVersion() const
{
	return m_pGameServer->GetClientVersion(m_ClientID);
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);

		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn(bool WeakHook)
{
	if(m_Team != TEAM_SPECTATORS)
	{
		m_WeakHookSpawn = WeakHook;
		m_Spawning = true;
	}
}

CCharacter *CPlayer::ForceSpawn(vec2 Pos)
{
	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, Pos);
	m_Team = 0;
	return m_pCharacter;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;

	char aBuf[512];
	DoChatMsg = false;
	if(DoChatMsg)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(m_ClientID), GameServer()->m_pController->GetTeamName(Team));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	if(Team == TEAM_SPECTATORS)
	{
		CGameControllerDDRace *Controller = (CGameControllerDDRace *)GameServer()->m_pController;
		if(g_Config.m_SvTeam != 3 && m_pCharacter)
		{
			// Joining spectators should not kill a locked team, but should still
			// check if the team finished by you leaving it.
			int DDRTeam = m_pCharacter->Team();
			Controller->m_Teams.SetForceCharacterTeam(m_ClientID, TEAM_FLOCK);
			Controller->m_Teams.CheckTeamFinished(DDRTeam);
		}
	}

	KillCharacter();

	m_Team = Team;
	m_LastSetTeam = Server()->Tick();
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	//GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	protocol7::CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = m_ClientID;
	Msg.m_Team = m_Team;
	Msg.m_Silent = !DoChatMsg;
	Msg.m_CooldownTick = m_LastSetTeam + Server()->TickSpeed() * g_Config.m_SvTeamChangeDelay;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(pPlayer && pPlayer->m_SpectatorID == m_ClientID)
				pPlayer->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

bool CPlayer::SetTimerType(int TimerType)
{
	if(TimerType == TIMERTYPE_DEFAULT)
	{
		if(Server()->IsSixup(m_ClientID))
			m_TimerType = TIMERTYPE_SIXUP;
		else
			SetTimerType(g_Config.m_SvDefaultTimerType);

		return true;
	}

	if(Server()->IsSixup(m_ClientID))
	{
		if(TimerType == TIMERTYPE_SIXUP || TimerType == TIMERTYPE_NONE)
		{
			m_TimerType = TimerType;
			return true;
		}
		else
			return false;
	}

	if(TimerType == TIMERTYPE_GAMETIMER)
	{
		if(GetClientVersion() >= VERSION_DDNET_GAMETICK)
			m_TimerType = TimerType;
		else
			return false;
	}
	else if(TimerType == TIMERTYPE_GAMETIMER_AND_BROADCAST)
	{
		if(GetClientVersion() >= VERSION_DDNET_GAMETICK)
			m_TimerType = TimerType;
		else
		{
			m_TimerType = TIMERTYPE_BROADCAST;
			return false;
		}
	}
	else
		m_TimerType = TimerType;

	return true;
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;

	if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos))
		return;

	m_WeakHookSpawn = false;
	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));

	if(g_Config.m_SvTeam == 3)
		m_pCharacter->SetSolo(true);
}

bool CPlayer::AfkTimer(int NewTargetX, int NewTargetY)
{
	/*
		afk timer (x, y = mouse coordinates)
		Since a player has to move the mouse to play, this is a better method than checking
		the player's position in the game world, because it can easily be bypassed by just locking a key.
		Frozen players could be kicked as well, because they can't move.
		It also works for spectators.
		returns true if kicked
	*/

	if(Server()->GetAuthedState(m_ClientID))
		return false; // don't kick admins
	if(g_Config.m_SvMaxAfkTime == 0)
		return false; // 0 = disabled

	if (m_LastPlaytime == 0) {
		UpdatePlaytime();
		return false;
	}

	if(NewTargetX != m_LastTarget_x || NewTargetY != m_LastTarget_y)
	{
		UpdatePlaytime();
		m_LastTarget_x = NewTargetX;
		m_LastTarget_y = NewTargetY;
		m_Sent1stAfkWarning = 0; // afk timer's 1st warning after 50% of sv_max_afk_time
		m_Sent2ndAfkWarning = 0;
	}
	else
	{
		if(!m_Paused)
		{
			// not playing, check how long
			if(m_Sent1stAfkWarning == 0 && m_LastPlaytime < time_get() - time_freq() * (int)(g_Config.m_SvMaxAfkTime * 0.5))
			{
				str_format(m_pAfkMsg, sizeof(m_pAfkMsg),
					"You have been afk for %d seconds now. Please note that you get kicked after not playing for %d seconds.",
					(int)(g_Config.m_SvMaxAfkTime * 0.5),
					g_Config.m_SvMaxAfkTime);
				m_pGameServer->SendChatTarget(m_ClientID, m_pAfkMsg);
				m_Sent1stAfkWarning = 1;
			}
			else if(m_Sent2ndAfkWarning == 0 && m_LastPlaytime < time_get() - time_freq() * (int)(g_Config.m_SvMaxAfkTime * 0.9))
			{
				str_format(m_pAfkMsg, sizeof(m_pAfkMsg),
					"You have been afk for %d seconds now. Please note that you get kicked after not playing for %d seconds.",
					(int)(g_Config.m_SvMaxAfkTime * 0.9),
					g_Config.m_SvMaxAfkTime);
				m_pGameServer->SendChatTarget(m_ClientID, m_pAfkMsg);
				m_Sent2ndAfkWarning = 1;
			}
			else if(m_LastPlaytime < time_get() - time_freq() * g_Config.m_SvMaxAfkTime)
			{
				m_pGameServer->Server()->Kick(m_ClientID, "Away from keyboard");
				return true;
			}
		}
	}
	return false;
}

void CPlayer::UpdatePlaytime()
{
	m_LastPlaytime = time_get();
}

void CPlayer::AfkVoteTimer(CNetObj_PlayerInput *NewTarget)
{
	if(g_Config.m_SvMaxAfkVoteTime == 0)
		return;

	if(!m_pLastTarget)
	{
		m_pLastTarget = new CNetObj_PlayerInput(*NewTarget);
		m_LastPlaytime = 0;
		m_Afk = true;
		return;
	}
	else if(mem_comp(NewTarget, m_pLastTarget, sizeof(CNetObj_PlayerInput)) != 0)
	{
		UpdatePlaytime();
		mem_copy(m_pLastTarget, NewTarget, sizeof(CNetObj_PlayerInput));
	}
	else if(m_LastPlaytime < time_get() - time_freq() * g_Config.m_SvMaxAfkVoteTime)
	{
		m_Afk = true;
		return;
	}

	m_Afk = false;
}

void CPlayer::ProcessPause()
{
	if(m_ForcePauseTime && m_ForcePauseTime < Server()->Tick())
	{
		m_ForcePauseTime = 0;
		Pause(PAUSE_NONE, true);
	}

	if(m_Paused == PAUSE_SPEC && !m_pCharacter->IsPaused() && m_pCharacter->IsGrounded() && m_pCharacter->m_Pos == m_pCharacter->m_PrevPos)
	{
		m_pCharacter->Pause(true);
		GameServer()->CreateDeath(m_pCharacter->m_Pos, m_ClientID, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
		GameServer()->CreateSound(m_pCharacter->m_Pos, SOUND_PLAYER_DIE, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
	}
}

int CPlayer::Pause(int State, bool Force)
{
	if(State < PAUSE_NONE || State > PAUSE_SPEC) // Invalid pause state passed
		return 0;

	if(!m_pCharacter)
		return 0;

	char aBuf[128];
	if(State != m_Paused)
	{
		// Get to wanted state
		switch(State)
		{
		case PAUSE_PAUSED:
		case PAUSE_NONE:
			if(m_pCharacter->IsPaused()) // First condition might be unnecessary
			{
				if(!Force && m_LastPause && m_LastPause + (int64_t)g_Config.m_SvSpecFrequency * Server()->TickSpeed() > Server()->Tick())
				{
					GameServer()->SendChatTarget(m_ClientID, "Can't /spec that quickly.");
					return m_Paused; // Do not update state. Do not collect $200
				}
				m_pCharacter->Pause(false);
				GameServer()->CreatePlayerSpawn(m_pCharacter->m_Pos, m_pCharacter->Teams()->TeamMask(m_pCharacter->Team(), -1, m_ClientID));
			}
			// fall-thru
		case PAUSE_SPEC:
			if(g_Config.m_SvPauseMessages)
			{
				str_format(aBuf, sizeof(aBuf), (State > PAUSE_NONE) ? "'%s' speced" : "'%s' resumed", Server()->ClientName(m_ClientID));
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
			}
			break;
		}

		// Update state
		m_Paused = State;
		m_LastPause = Server()->Tick();

		// Sixup needs a teamchange
		protocol7::CNetMsg_Sv_Team Msg;
		Msg.m_ClientID = m_ClientID;
		Msg.m_CooldownTick = Server()->Tick();
		Msg.m_Silent = true;
		Msg.m_Team = m_Paused ? protocol7::TEAM_SPECTATORS : m_Team;

		GameServer()->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, m_ClientID);
	}

	return m_Paused;
}

int CPlayer::ForcePause(int Time)
{
	m_ForcePauseTime = Server()->Tick() + Server()->TickSpeed() * Time;

	if(g_Config.m_SvPauseMessages)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' was force-paused for %ds", Server()->ClientName(m_ClientID), Time);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	return Pause(PAUSE_SPEC, true);
}

int CPlayer::IsPaused()
{
	return m_ForcePauseTime ? m_ForcePauseTime : -1 * m_Paused;
}

bool CPlayer::IsPlaying()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return true;
	return false;
}

void CPlayer::SpectatePlayerName(const char *pName)
{
	if(!pName)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i != m_ClientID && Server()->ClientIngame(i) && !str_comp(pName, Server()->ClientName(i)))
		{
			m_SpectatorID = i;
			return;
		}
	}
}

void CPlayer::ProcessScoreResult(CScorePlayerResult &Result)
{
	/*
	if(Result.m_Success) // SQL request was successful
	{
		switch(Result.m_MessageKind)
		{
		case CScorePlayerResult::DIRECT:
			for(auto &aMessage : Result.m_Data.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChatTarget(m_ClientID, aMessage);
			}
			break;
		case CScorePlayerResult::ALL:
			for(auto &aMessage : Result.m_Data.m_aaMessages)
			{
				if(aMessage[0] == 0)
					break;
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aMessage, m_ClientID);
			}
			break;
		case CScorePlayerResult::BROADCAST:
			if(Result.m_Data.m_Broadcast[0] != 0)
				GameServer()->SendBroadcast(Result.m_Data.m_Broadcast, -1);
			break;
		case CScorePlayerResult::MAP_VOTE:
			GameServer()->m_VoteType = CGameContext::VOTE_TYPE_OPTION;
			GameServer()->m_LastMapVote = time_get();

			char aCmd[256];
			str_format(aCmd, sizeof(aCmd),
				"sv_reset_file types/%s/flexreset.cfg; change_map \"%s\"",
				Result.m_Data.m_MapVote.m_Server, Result.m_Data.m_MapVote.m_Map);

			char aChatmsg[512];
			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)",
				Server()->ClientName(m_ClientID), Result.m_Data.m_MapVote.m_Map, "/map");

			GameServer()->CallVote(m_ClientID, Result.m_Data.m_MapVote.m_Map, aCmd, "/map", aChatmsg);
			break;
		case CScorePlayerResult::PLAYER_INFO:
			// GameServer()->Score()->PlayerData(m_ClientID)->Set(Result.m_Data.m_Info.m_Time, Result.m_Data.m_Info.m_CpTime);
			m_Score = -9999; // Result.m_Data.m_Info.m_Score;
			m_HasFinishScore = Result.m_Data.m_Info.m_HasFinishScore;
			// -9999 stands for no time and isn't displayed in scoreboard, so
			// shift the time by a second if the player actually took 9999
			// seconds to finish the map.
			if(m_HasFinishScore && m_Score == -9999)
				m_Score = -10000;
			Server()->ExpireServerInfo();
			int Birthday = Result.m_Data.m_Info.m_Birthday;
			if(Birthday != 0)
			{
				char aBuf[512];
				str_format(aBuf, sizeof(aBuf),
					"Happy DDNet birthday to %s for finishing their first map %d year%s ago!",
					Server()->ClientName(m_ClientID), Birthday, Birthday > 1 ? "s" : "");
				GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, m_ClientID);
				str_format(aBuf, sizeof(aBuf),
					"Happy DDNet birthday, %s!\nYou have finished your first map exactly %d year%s ago!",
					Server()->ClientName(m_ClientID), Birthday, Birthday > 1 ? "s" : "");
				GameServer()->SendBroadcast(aBuf, m_ClientID);
			}
			break;
		}
	}
	*/
}
