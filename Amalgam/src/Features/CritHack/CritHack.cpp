#include "CritHack.h"

#include "../Ticks/Ticks.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

#define WEAPON_RANDOM_RANGE				10000
#define TF_DAMAGE_CRIT_MULTIPLIER		3.0f
#define TF_DAMAGE_CRIT_CHANCE			0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID		0.02f
#define TF_DAMAGE_CRIT_CHANCE_MELEE		0.15f
#define TF_DAMAGE_CRIT_DURATION_RAPID	2.0f

#define STATS_SEND_FREQUENCY 1.f

#define SEED_ATTEMPTS 4096
#define BUCKET_ATTEMPTS 1000

//#define SERVER_CRIT_DATA

int CCritHack::GetCritCommand(CTFWeaponBase* pWeapon, int iCommandNumber, bool bCrit, bool bSafe)
{
	for (int i = iCommandNumber; i < iCommandNumber + SEED_ATTEMPTS; i++)
	{
		if (IsCritCommand(i, pWeapon, bCrit, bSafe))
			return i;
	}
	return 0;
}

bool CCritHack::IsCritCommand(int iCommandNumber, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	int iSeed = CommandToSeed(iCommandNumber);
	return IsCritSeed(iSeed, pWeapon, bCrit, bSafe);
}

bool CCritHack::IsCritSeed(int iSeed, CTFWeaponBase* pWeapon, bool bCrit, bool bSafe)
{
	if (iSeed == pWeapon->m_iCurrentSeed())
		return false;

	SDK::RandomSeed(iSeed);
	int iRandom = SDK::RandomInt(0, WEAPON_RANDOM_RANGE - 1);

	if (bSafe)
	{
		int iLower, iUpper;
		if (m_bMelee)
			iLower = 1500, iUpper = 6000;
		else
			iLower = 100, iUpper = 800;
		iLower *= m_flMultCritChance, iUpper *= m_flMultCritChance;

		if (bCrit ? iLower >= 0 : iUpper < WEAPON_RANDOM_RANGE)
			return bCrit ? iRandom < iLower : !(iRandom < iUpper);
	}

	int iRange = m_flCritChance * WEAPON_RANDOM_RANGE;
	return bCrit ? iRandom < iRange : !(iRandom < iRange);
}

int CCritHack::CommandToSeed(int iCommandNumber)
{
	int iSeed = MD5_PseudoRandom(iCommandNumber) & std::numeric_limits<int>::max();
	int iMask = m_bMelee
		? m_iEntIndex << 16 | I::EngineClient->GetLocalPlayer() << 8
		: m_iEntIndex << 8 | I::EngineClient->GetLocalPlayer();
	return iSeed ^ iMask;
}



void CCritHack::UpdateWeaponInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_iEntIndex = pWeapon->entindex();
	m_bMelee = pWeapon->GetSlot() == SLOT_MELEE;
	if (m_bMelee)
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_MELEE * pLocal->GetCritMult();
	else if (pWeapon->IsRapidFire())
	{
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_RAPID * pLocal->GetCritMult();
		float flNonCritDuration = (TF_DAMAGE_CRIT_DURATION_RAPID / m_flCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
		m_flCritChance = 1.f / flNonCritDuration;
	}
	else
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE * pLocal->GetCritMult();
	m_flMultCritChance = SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon);
	m_flCritChance *= m_flMultCritChance;



	static CTFWeaponBase* pStaticWeapon = nullptr;
	const CTFWeaponBase* pOldWeapon = pStaticWeapon;
	pStaticWeapon = pWeapon;

	static float flStaticBucket = 0.f;
	const float flLastBucket = flStaticBucket;
	const float flBucket = flStaticBucket = pWeapon->m_flCritTokenBucket();

	static int iStaticCritChecks = 0.f;
	const int iLastCritChecks = iStaticCritChecks;
	const int iCritChecks = iStaticCritChecks = pWeapon->m_nCritChecks();

	static int iStaticCritSeedRequests = 0.f;
	const int iLastCritSeedRequests = iStaticCritSeedRequests;
	const int iCritSeedRequests = iStaticCritSeedRequests = pWeapon->m_nCritSeedRequests();

	if (pWeapon == pOldWeapon && flBucket == flLastBucket && iCritChecks == iLastCritChecks && iCritSeedRequests == iLastCritSeedRequests)
		return;

	static auto tf_weapon_criticals_bucket_cap = H::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap->GetFloat();
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot(false);
	if (!m_bMelee && nProjectilesPerShot > 0)
		nProjectilesPerShot = SDK::AttribHookValue(nProjectilesPerShot, "mult_bullets_per_shot", pWeapon);
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	if (bRapidFire)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	float flMult = m_bMelee ? 0.5f : Math::RemapVal(float(iCritSeedRequests + 1) / (iCritChecks + 1), 0.1f, 1.f, 1.f, 3.f);
	float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;

	int iPotentialCrits = (std::max(flBucketCap, flBucket) - flBaseDamage) / (TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (m_bMelee ? 2 : 1) - flBaseDamage);
	int iAvailableCrits = 0;
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			iTestShots++; iTestCrits++;

			float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits) / iTestShots, 0.1f, 1.f, 1.f, 3.f);
			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);
			flTestBucket -= flCost * flTestMult;
			if (flTestBucket < 0.f)
				break;

			iAvailableCrits++;
		}
	}

	int iNextCrit = 0;
	if (iAvailableCrits != iPotentialCrits)
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		float flTickBase = I::GlobalVars->curtime;
		float flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		for (int i = 0; i < BUCKET_ATTEMPTS; i++)
		{
			int iCrits = 0;
			{
				int iTestShots2 = iTestShots, iTestCrits2 = iTestCrits;
				float flTestBucket2 = flTestBucket;
				for (int j = 0; j < BUCKET_ATTEMPTS; j++)
				{
					iTestShots2++; iTestCrits2++;

					float flTestMult = m_bMelee ? 0.5f : Math::RemapVal(float(iTestCrits2) / iTestShots2, 0.1f, 1.f, 1.f, 3.f);
					if (flTestBucket2 < flBucketCap)
						flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, flBucketCap);
					flTestBucket2 -= flCost * flTestMult;
					if (flTestBucket2 < 0.f)
						break;

					iCrits++;
				}
			}
			if (iAvailableCrits < iCrits)
				break;

			if (!bRapidFire)
				iTestShots++;
			else
			{
				flTickBase += std::ceilf(flFireRate / TICK_INTERVAL) * TICK_INTERVAL;
				if (flTickBase >= flLastRapidFireCritCheckTime + 1.f || !i && flTestBucket == flBucketCap)
				{
					iTestShots++;
					flLastRapidFireCritCheckTime = flTickBase;
				}
			}

			if (flTestBucket < flBucketCap)
				flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);

			iNextCrit++;
		}
	}

	m_flDamage = flBaseDamage;
	m_flCost = flCost * flMult;
	m_iPotentialCrits = iPotentialCrits;
	m_iAvailableCrits = iAvailableCrits;
	m_iNextCrit = iNextCrit;
}

void CCritHack::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	UpdateWeaponInfo(pLocal, pWeapon);

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;
	if (!m_bMelee)
	{
		const float flNormalizedDamage = m_iCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
		float flCritChance = m_flCritChance + 0.1f;
		if (m_iRangedDamage && m_iCritDamage)
		{
			const float flObservedCritChance = flNormalizedDamage / (flNormalizedDamage + m_iRangedDamage - m_iCritDamage);
			m_bCritBanned = flObservedCritChance > flCritChance;
		}

		if (m_bCritBanned)
			m_flDamageTilFlip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2 - m_iRangedDamage;
		else
			m_flDamageTilFlip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + m_iRangedDamage - m_iCritDamage)) / (flCritChance - 1);
	}

	if (auto pResource = H::Entities.GetResource())
	{
		m_iResourceDamage = pResource->m_iDamage(I::EngineClient->GetLocalPlayer());
		/* // more of a proof of concept for resyncing crit damage
		if (m_flLastDamageTime < I::GlobalVars->curtime + STATS_SEND_FREQUENCY * 2)
		{	// attempt to resync damages
			m_iRangedDamage = m_iResourceDamage - m_iMeleeDamage;

			float flObservedCritChance = pWeapon->m_flObservedCritChance();
			m_iCritDamage = (TF_DAMAGE_CRIT_MULTIPLIER * flObservedCritChance * m_iResourceDamage) / (1 + 2 * flObservedCritChance);
			SDK::Output("Info", std::format("{}, {}", m_iRangedDamage, m_iCritDamage).c_str());
		}
		*/
		m_iDesyncDamage = m_iRangedDamage + m_iMeleeDamage - m_iResourceDamage;
	}
}

bool CCritHack::WeaponCanCrit(CTFWeaponBase* pWeapon, bool bWeaponOnly)
{
	if (!bWeaponOnly && !pWeapon->AreRandomCritsEnabled() || SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon) <= 0.f)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_KNIFE:
	case TF_WEAPON_PASSTIME_GUN:
		return false;
	}

	return true;
}

void CCritHack::Reset()
{
	m_iCritDamage = 0;
	m_iRangedDamage = 0;
	m_iMeleeDamage = 0;
	m_iResourceDamage = 0;
	m_iDesyncDamage = 0;

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;

	m_mHealthHistory.clear();

	//m_flLastDamageTime = 0.f;
}



int CCritHack::GetCritRequest(CUserCmd* pCmd, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	bool bCanCrit = m_iAvailableCrits > 0 && !m_bCritBanned;

	bool bForce = bCanCrit && Vars::CritHack::ForceCrits.Value;
	if (bCanCrit && m_bMelee && Vars::CritHack::AlwaysMeleeCrit.Value
		&& (Vars::Aimbot::General::AutoShoot.Value ? pCmd->buttons & IN_ATTACK && !(G::OriginalCmd.buttons & IN_ATTACK) : Vars::Aimbot::General::AimType.Value)
		&& G::AimTarget.m_iEntIndex)
	{
		auto pEntity = I::ClientEntityList->GetClientEntity(G::AimTarget.m_iEntIndex)->As<CBaseEntity>();
		if (pEntity && pEntity->IsPlayer() && (SDK::FriendlyFire() || pLocal->m_iTeamNum() != pEntity->m_iTeamNum()))
			bForce = true;
	}

	bool bSkip = Vars::CritHack::AvoidRandomCrits.Value;
	bool bDesync = CommandToSeed(pCmd->command_number) == pWeapon->m_iCurrentSeed();

	return bForce ? CritRequestEnum::Crit : bSkip || bDesync ? CritRequestEnum::Skip : CritRequestEnum::Any;
}

void CCritHack::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || !I::EngineClient->IsInGame())
		return;

	UpdateInfo(pLocal, pWeapon);
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
		return;

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && pCmd->buttons & IN_ATTACK)
		pCmd->buttons &= ~IN_ATTACK2;

	bool bAttacking = G::Attacking /*== 1*/ || F::Ticks.m_bDoubletap || F::Ticks.m_bSpeedhack;
	if (m_bMelee)
	{
		bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK;
		if (!bAttacking && pWeapon->GetWeaponID() == TF_WEAPON_FISTS)
			bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK2;
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && !(G::LastUserCmd->buttons & IN_ATTACK))
		bAttacking = false;
	else if (!bAttacking)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			if (pWeapon->IsInReload() && G::CanPrimaryAttack && SDK::AttribHookValue(0, "can_overload", pWeapon))
			{
				int iClip1 = pWeapon->m_iClip1();
				if (pWeapon->m_bRemoveable() && iClip1 > 0)
					bAttacking = true;
				else if (iClip1 >= pWeapon->GetMaxClip1() || iClip1 > 0 && pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) == 0)
					bAttacking = true;
			}
		}
	}
	if (!bAttacking || pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
		return;

	int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
	if (iRequest == CritRequestEnum::Any)
		return;

	if (!F::AntiCheatCompatibility.Active())
	{
		if (int iCommand = GetCritCommand(pWeapon, pCmd->command_number, iRequest == CritRequestEnum::Crit))
		{
			pCmd->command_number = iCommand;
			pCmd->random_seed = MD5_PseudoRandom(iCommand) & std::numeric_limits<int>::max();
		}
	}
	else if (Vars::Misc::Game::AntiCheatCritHack.Value)
	{
		if (!IsCritCommand(pCmd->command_number, pWeapon, iRequest == CritRequestEnum::Crit, false))
		{
			pCmd->buttons &= ~IN_ATTACK;
			pCmd->viewangles = G::OriginalCmd.viewangles;
			G::PSilentAngles = false;
		}
	}
}

int CCritHack::PredictCmdNum(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	auto fGetCmdNum = [&](int iCommandNumber)
		{
			if (!pWeapon || !pLocal->IsAlive() || !I::EngineClient->IsInGame() || F::AntiCheatCompatibility.Active()
				|| pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !WeaponCanCrit(pWeapon))
				return iCommandNumber;

			UpdateInfo(pLocal, pWeapon);
			if (pWeapon->IsRapidFire() && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
				return iCommandNumber;

			int iRequest = GetCritRequest(pCmd, pLocal, pWeapon);
			if (iRequest == CritRequestEnum::Any)
				return iCommandNumber;

			if (int iCommand = GetCritCommand(pWeapon, iCommandNumber, iRequest == CritRequestEnum::Crit))
				return iCommand;
			return iCommandNumber;
		};

	static int iCommandNumber = 0; // cache, don't constantly test

	static int iStaticCommand = 0;
	if (pCmd->command_number != iStaticCommand)
	{
		iCommandNumber = fGetCmdNum(pCmd->command_number);
		iStaticCommand = pCmd->command_number;
	}

	return iCommandNumber;
}

bool CCritHack::ShouldForceEffects(CTFPlayer* pLocal)
{
	if (!Vars::CritHack::CritEffects.Value || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->IsCritBoosted())
		return false;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !WeaponCanCrit(pWeapon))
		return false;

	float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());
	return Vars::CritHack::ForceCrits.Value && !m_bCritBanned && m_iAvailableCrits > 0 || pWeapon->m_flCritTime() > flTickBase;
}

void CCritHack::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!pLocal)
			return;

		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		int iDamage = pEvent->GetInt("damageamount");
		int iHealth = pEvent->GetInt("health");
		int iWeaponID = pEvent->GetInt("weaponid");

		if (m_mHealthHistory.contains(iVictim))
		{
			auto& tHistory = m_mHealthHistory[iVictim];
			auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CTFPlayer>();

			if (!iHealth)
			{
				iDamage = std::clamp(iDamage, 0, tHistory.m_iNewHealth);
				tHistory.m_iSpawnCounter = -1;
			}
			else if (pVictim && (pVictim->m_bFeignDeathReady() || pVictim->InCond(TF_COND_FEIGN_DEATH))) // damage number is spoofed upon sending, correct it
			{
				int iOldHealth = (tHistory.m_mHistory.contains(iHealth) ? tHistory.m_mHistory[iHealth].m_iOldHealth : tHistory.m_iNewHealth) % 32768;
				if (iHealth > iOldHealth)
				{
					for (auto& tOldHealth : tHistory.m_mHistory | std::views::values)
					{
						int iOldHealth2 = tOldHealth.m_iOldHealth % 32768;
						if (iOldHealth2 > iHealth)
							iOldHealth = iHealth > iOldHealth ? iOldHealth2 : std::min(iOldHealth, iOldHealth2);
					}
				}
				iDamage = std::clamp(iOldHealth - iHealth, 0, iDamage);
			}
		}
		if (iHealth)
			StoreHealthHistory(iVictim, iHealth);

		if (iVictim == iAttacker || iAttacker != I::EngineClient->GetLocalPlayer())
			return;

		if (auto pGameRules = I::TFGameRules())
		{
			auto pMatchDesc = pGameRules->GetMatchGroupDescription();
			if (pMatchDesc && pGameRules->m_iRoundState() != GR_STATE_RND_RUNNING)
			{
				switch (pMatchDesc->m_eMatchType)
				{
				case MATCH_TYPE_COMPETITIVE:
				case MATCH_TYPE_CASUAL:
					return;
				}
			}

			const int iInsanePlayerDamage = pGameRules->m_bPlayingMannVsMachine() ? 5000 : 1500;
			if (iDamage > iInsanePlayerDamage)
				return;
		}

		//m_flLastDamageTime = I::GlobalVars->curtime;

		CTFWeaponBase* pWeapon = nullptr;
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon2 = pLocal->GetWeaponFromSlot(i);
			if (!pWeapon2 || pWeapon2->GetWeaponID() != iWeaponID)
				continue;

			pWeapon = pWeapon2;
			break;
		}

		if (!pWeapon || pWeapon->GetSlot() != SLOT_MELEE)
		{
			m_iRangedDamage += iDamage;
			if (bCrit && !pLocal->IsCritBoosted())
				m_iCritDamage += iDamage;
		}
		else
			m_iMeleeDamage += iDamage;

		return;
	}
	case FNV1A::Hash32Const("player_spawn"):
	{
		int iIndex = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));

		if (m_mHealthHistory.contains(iIndex))
		{
			auto& tHistory = m_mHealthHistory[iIndex];

			tHistory.m_iSpawnCounter = -1;
		}

		return;
	}
	case FNV1A::Hash32Const("scorestats_accumulated_update"):
	case FNV1A::Hash32Const("mvm_reset_stats"):
	{
		m_iRangedDamage = m_iCritDamage = m_iMeleeDamage = 0;
		return;
	}
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
	{
		Reset();
	}
	}
}

void CCritHack::Store()
{
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		auto pPlayer = I::ClientEntityList->GetClientEntity(n)->As<CTFPlayer>();
		if (pPlayer && pPlayer->IsAlive() && !pPlayer->IsAGhost())
			StoreHealthHistory(n, pPlayer->m_iHealth(), pPlayer);
	}
}

void CCritHack::StoreHealthHistory(int iIndex, int iHealth, CTFPlayer* pPlayer)
{
	bool bContains = m_mHealthHistory.contains(iIndex);
	auto& tHistory = m_mHealthHistory[iIndex];

	if (bContains && pPlayer)
	{	// deal with instant respawn damage desync better
		if (pPlayer->IsDormant())
			tHistory.m_iSpawnCounter = -1;
		else if (tHistory.m_iSpawnCounter == -1)
			tHistory.m_iSpawnCounter = pPlayer->m_iSpawnCounter();
		else if (tHistory.m_iSpawnCounter != pPlayer->m_iSpawnCounter())
			return; // wait for spawn
	}

	if (!bContains)
		tHistory = { iHealth, iHealth };
	else if (iHealth != tHistory.m_iNewHealth)
	{
		tHistory.m_iOldHealth = std::max(tHistory.m_iNewHealth, iHealth);
		tHistory.m_iNewHealth = iHealth;
	}

	tHistory.m_mHistory[iHealth % 32768] = { tHistory.m_iOldHealth, float(SDK::PlatFloatTime()) };
	while (tHistory.m_mHistory.size() > 3)
	{
		int iIndex2; float flMin = std::numeric_limits<float>::max();
		for (auto& [i, tStorage] : tHistory.m_mHistory)
		{
			if (tStorage.m_flTime < flMin)
				flMin = tStorage.m_flTime, iIndex2 = i;
		}
		tHistory.m_mHistory.erase(iIndex2);
	}
}

#ifdef SERVER_CRIT_DATA
MAKE_SIGNATURE(CTFGameStats_FindPlayerStats, "server.dll", "4C 8B C1 48 85 D2 75", 0x0);
MAKE_SIGNATURE(UTIL_PlayerByIndex, "server.dll", "48 83 EC ? 8B D1 85 C9 7E ? 48 8B 05", 0x0);

static void* s_pCTFGameStats = nullptr;
MAKE_HOOK(CTFGameStats_FindPlayerStats, S::CTFGameStats_FindPlayerStats(), void*,
	void* rcx, CBasePlayer* pPlayer)
{
	DEBUG_RETURN(CTFGameStats_FindPlayerStats, rcx, pPlayer);

	s_pCTFGameStats = rcx;
	return CALL_ORIGINAL(rcx, pPlayer);
}
#endif
void CCritHack::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::CritHack) || !I::EngineClient->IsInGame())
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost() || !WeaponCanCrit(pWeapon, true))
		return;

	const DragBox_t dtPos = Vars::Menu::CritsDisplay.Value;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);

	if (!pWeapon->AreRandomCritsEnabled())
	{
		H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Color_t(255, 255, 255, 255), Vars::Menu::Theme::Background.Value, ALIGN_TOP, "Random crits disabled");
		return;
	}

	float flTickBase = TICKS_TO_TIME(pLocal->m_nTickBase());

	// Calculate bar values
	float flProgress = 0.f;
	int iAvailableCrits = m_iAvailableCrits;
	int iPotentialCrits = m_iPotentialCrits;
	bool bIsCritReady = false;
	bool bIsCritBanned = m_bCritBanned;

	if (F::AntiCheatCompatibility.Active())
	{
		H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Color_t(255, 255, 255, 255), Vars::Menu::Theme::Background.Value, ALIGN_TOP, "Anticheat compatibility");
		return;
	}

	if (pLocal->IsCritBoosted())
	{
		flProgress = 1.0f;
		bIsCritReady = true;
	}
	else if (pWeapon->m_flCritTime() > flTickBase)
	{
		float flTime = pWeapon->m_flCritTime() - flTickBase;
		H::Draw.StringOutlined(fFont, dtPos.x, dtPos.y + 2, Color_t(255, 255, 255, 255), Vars::Menu::Theme::Background.Value, ALIGN_TOP, std::string("Streaming crits " + std::to_string(flTime) + "s").c_str());
		return;
	}
	else if (!bIsCritBanned && iPotentialCrits > 0)
	{
		flProgress = float(iAvailableCrits) / float(iPotentialCrits);
		flProgress = std::clamp(flProgress, 0.0f, 1.0f);

		if (iAvailableCrits > 0)
		{
			bIsCritReady = true;
		}
	}
	else if (bIsCritBanned)
	{
		float flDamageNeeded = ceilf(m_flDamageTilFlip);
		flProgress = std::min(flDamageNeeded / 500.0f, 1.0f);
	}

	// --- DIMENSIONS ---
	constexpr int PANEL_WIDTH = 180;
	const int scaledWidth = H::Draw.Scale(PANEL_WIDTH, Scale_Round);
	const int panelX = dtPos.x - scaledWidth / 2;
	const int panelY = dtPos.y - H::Draw.Scale(6, Scale_Round);

	// --- 1. STATUS TEXT & CRIT COUNT ---
	std::string sStatusText;
	std::string sCritCount;

	if (bIsCritBanned)
	{
		sStatusText = "⚠ DEAL " + std::to_string((int)ceilf(m_flDamageTilFlip)) + " DMG";
		sCritCount = std::to_string(iAvailableCrits) + " / " + std::to_string(iPotentialCrits) + " crits (banned)";
	}
	else if (bIsCritReady)
	{
		sStatusText = "⚡ CRIT READY";
		sCritCount = std::to_string(iAvailableCrits) + " / " + std::to_string(iPotentialCrits) + " crits";
	}
	else if (iPotentialCrits > 0)
	{
		int iShots = m_iNextCrit;
		if (iShots > 0 && iAvailableCrits == 0)
		{
			sStatusText = "⚡ CRIT IN " + std::to_string(iShots) + " SHOT" + (iShots == 1 ? "" : "S");
		}
		else
		{
			sStatusText = "⚡ BUILDING CRITS";
		}
		sCritCount = std::to_string(iAvailableCrits) + " / " + std::to_string(iPotentialCrits) + " crits";
	}

	// Draw status text
	if (!sStatusText.empty())
	{
		Color_t textColor = bIsCritBanned ? Color_t(255, 50, 50, 255) : Color_t(255, 255, 255, 255);
		H::Draw.String(
			fFont,
			dtPos.x,
			panelY,
			textColor,
			ALIGN_TOP,
			sStatusText.c_str()
		);
	}

	// --- 2. MODERN GLASS TICK BAR ---
	const int barHeight = H::Draw.Scale(14, Scale_Round);
	const int barY = panelY + fFont.m_nTall + H::Draw.Scale(4, Scale_Round);
	const int barWidth = scaledWidth;
	const int barX = panelX;

	// Bar background
	H::Draw.FillRect(
		barX,
		barY,
		barWidth,
		barHeight,
		Color_t(10, 15, 25, 180)
	);

	// Bar inner glow
	H::Draw.FillRect(
		barX + H::Draw.Scale(1, Scale_Round),
		barY + H::Draw.Scale(1, Scale_Round),
		barWidth - H::Draw.Scale(2, Scale_Round),
		barHeight - H::Draw.Scale(2, Scale_Round),
		Color_t(0, 0, 0, 100)
	);

	// Progress fill with glassy gradient
	if (flProgress > 0.0f)
	{
		const int fillWidth = static_cast<int>(barWidth * flProgress);
		const int fillX = barX + H::Draw.Scale(1, Scale_Round);
		const int fillY = barY + H::Draw.Scale(1, Scale_Round);
		const int fillH = barHeight - H::Draw.Scale(2, Scale_Round);

		// Choose color based on state
		Color_t fillColor;
		if (bIsCritBanned)
			fillColor = Color_t(255, 50, 50, 255); // Red when banned
		else if (bIsCritReady)
			fillColor = Vars::Menu::Theme::Accent.Value; // Theme color when ready
		else
			fillColor = Vars::Menu::Theme::Accent.Value; // Theme color when building

		fillColor.a = 180 + static_cast<int>(flProgress * 75);

		H::Draw.FillRect(
			fillX,
			fillY,
			fillWidth,
			fillH,
			fillColor
		);

		// Glass shine on top of fill
		H::Draw.FillRect(
			fillX,
			fillY,
			fillWidth,
			fillH / 2,
			Color_t(255, 255, 255, 30 + static_cast<int>(flProgress * 40))
		);

		// Fill edge glow
		if (flProgress < 0.95f && fillWidth > H::Draw.Scale(10, Scale_Round))
		{
			H::Draw.FillRect(
				fillX + fillWidth - H::Draw.Scale(2, Scale_Round),
				fillY,
				H::Draw.Scale(2, Scale_Round),
				fillH,
				Color_t(255, 255, 255, 40 + static_cast<int>(flProgress * 50))
			);
		}
	}

	// Bar border
	H::Draw.LineRect(
		barX,
		barY,
		barWidth,
		barHeight,
		Color_t(255, 255, 255, 15 + static_cast<int>(flProgress * 30))
	);

	// --- 3. COMPLETION PULSE (only when crit ready, not banned) ---
	if (bIsCritReady && !bIsCritBanned)
	{
		const float pulse = (sinf(I::GlobalVars->curtime * 3.0f) + 1.0f) / 2.0f;
		const int pulseAlpha = 20 + static_cast<int>(pulse * 60);

		// Pulse glow using theme color
		Color_t pulseColor = Vars::Menu::Theme::Accent.Value;
		pulseColor.a = pulseAlpha;

		H::Draw.FillRect(
			barX - H::Draw.Scale(2, Scale_Round),
			barY - H::Draw.Scale(2, Scale_Round),
			barWidth + H::Draw.Scale(4, Scale_Round),
			barHeight + H::Draw.Scale(4, Scale_Round),
			pulseColor
		);

		// Inner pulse glow
		Color_t innerPulse = Vars::Menu::Theme::Accent.Value;
		innerPulse.a = pulseAlpha / 2;

		H::Draw.FillRect(
			barX,
			barY - H::Draw.Scale(1, Scale_Round),
			barWidth,
			barHeight + H::Draw.Scale(2, Scale_Round),
			innerPulse
		);
	}

	// --- 4. CRIT COUNT TEXT (below the bar) ---
	if (!sCritCount.empty())
	{
		Color_t countColor = bIsCritBanned ? Color_t(255, 50, 50, 200) : Color_t(255, 255, 255, 200);
		H::Draw.String(
			fFont,
			dtPos.x,
			barY + barHeight + H::Draw.Scale(4, Scale_Round),
			countColor,
			ALIGN_TOP,
			sCritCount.c_str()
		);
	}

	// --- 5. TICK MARKS ---
	{
		const int tickMarkCount = std::min(8, iPotentialCrits);
		if (tickMarkCount > 1 && iPotentialCrits > 0)
		{
			const float tickSpacing = static_cast<float>(barWidth - H::Draw.Scale(2, Scale_Round)) / tickMarkCount;
			const int tickY = barY + H::Draw.Scale(2, Scale_Round);
			const int tickH = barHeight - H::Draw.Scale(4, Scale_Round);

			for (int i = 1; i < tickMarkCount; i++)
			{
				const int tickX = barX + H::Draw.Scale(1, Scale_Round) + static_cast<int>(i * tickSpacing);
				const bool isFilled = static_cast<float>(i) / tickMarkCount <= flProgress;

				H::Draw.Line(
					tickX,
					tickY,
					tickX,
					tickY + tickH,
					Color_t(255, 255, 255, isFilled ? 20 + static_cast<int>(flProgress * 30) : 8)
				);
			}
		}
	}

	// Debug info (unchanged)
	if (Vars::Debug::Info.Value)
	{
		int iDebugY = barY + barHeight + fFont.m_nTall + H::Draw.Scale(8, Scale_Round);
		H::Draw.StringOutlined(fFont, dtPos.x, iDebugY, Color_t(255, 255, 255, 255), Vars::Menu::Theme::Background.Value, ALIGN_TOP,
			("RangedDamage: " + std::to_string(m_iRangedDamage) + ", CritDamage: " + std::to_string(m_iCritDamage)).c_str());
		iDebugY += fFont.m_nTall + 2;
		H::Draw.StringOutlined(fFont, dtPos.x, iDebugY, Color_t(255, 255, 255, 255), Vars::Menu::Theme::Background.Value, ALIGN_TOP,
			("Bucket: " + std::to_string(pWeapon->m_flCritTokenBucket()) + ", Shots: " + std::to_string(pWeapon->m_nCritChecks()) + ", Crits: " + std::to_string(pWeapon->m_nCritSeedRequests())).c_str());
	}
}
