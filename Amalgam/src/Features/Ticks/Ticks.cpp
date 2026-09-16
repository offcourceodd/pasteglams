#include "Ticks.h"

#include "../PacketManip/AntiAim/AntiAim.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../Backtrack/Backtrack.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"

void CTicks::Reset()
{
	m_bSpeedhack = m_bDoubletap = m_bRecharge = m_bWarp = false;
	m_iShiftedTicks = m_iShiftedGoal = 0;
}

void CTicks::Recharge(CTFPlayer* pLocal)
{
	if (!m_bGoalReached)
		return;

	bool bPassive = m_bRecharge = false;

	static float flPassiveTime = 0.f;
	flPassiveTime = std::max(flPassiveTime - TICK_INTERVAL, -TICK_INTERVAL);
	if (Vars::Doubletap::PassiveRecharge.Value && 0.f >= flPassiveTime)
	{
		bPassive = true;
		flPassiveTime += 1.f / Vars::Doubletap::PassiveRecharge.Value;
	}

	if (m_iDeficit)
	{
		bPassive = true;
		m_iDeficit--, m_iShiftedTicks--;
	}

	if (!Vars::Doubletap::RechargeTicks.Value && !bPassive
		|| m_bDoubletap || m_bWarp || m_iShiftedTicks == m_iMaxShift || m_bSpeedhack)
		return;

	m_bRecharge = true;
	m_iShiftedGoal = m_iShiftedTicks + 1;
}

void CTicks::Warp()
{
	if (!m_bGoalReached)
		return;

	m_bWarp = false;
	if (!Vars::Doubletap::Warp.Value
		|| !m_iShiftedTicks || m_bDoubletap || m_bRecharge || m_bSpeedhack)
		return;

	m_bWarp = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::WarpRate.Value + 1, 0);
}

void CTicks::Doubletap(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!m_bGoalReached)
		return;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack)
		return;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return;

	bool bAttacking = G::PrimaryWeaponType == EWeaponType::MELEE ? pCmd->buttons & IN_ATTACK : G::Attacking;
	if (!G::CanPrimaryAttack && !G::Reloading || !bAttacking && !m_bDoubletap || F::AutoRocketJump.IsRunning())
		return;

	m_bDoubletap = true;
	m_iShiftedGoal = std::max(m_iShiftedTicks - Vars::Doubletap::TickLimit.Value + 1, 0);
	if (Vars::Doubletap::AntiWarp.Value)
		m_bAntiWarp = pLocal->m_hGroundEntity();
}

void CTicks::Speedhack()
{
	m_bSpeedhack = Vars::Speedhack::Scale.Value != 1;
	if (!m_bSpeedhack)
		return;

	m_bDoubletap = m_bWarp = m_bRecharge = false;
}

static Vec3 s_vVelocity = {};
static int s_iMaxTicks = 0;
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove, int iTicks)
{
	s_iMaxTicks = std::max(iTicks + 1, s_iMaxTicks);

	Vec3 vAngles; Math::VectorAngles(s_vVelocity, vAngles);
	vAngles.y = flYaw - vAngles.y;
	Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
	vForward *= s_vVelocity.Length2D();

	if (iTicks > std::max(s_iMaxTicks - 8, 3))
		flForwardMove = -vForward.x, flSideMove = -vForward.y;
	else if (iTicks > 3)
		flForwardMove = flSideMove = 0.f;
	else
		flForwardMove = vForward.x, flSideMove = vForward.y;
}
void CTicks::AntiWarp(CTFPlayer* pLocal, float flYaw, float& flForwardMove, float& flSideMove)
{
	AntiWarp(pLocal, flYaw, flForwardMove, flSideMove, GetTicks());
}
void CTicks::AntiWarp(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bAntiWarp)
		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	else
	{
		s_vVelocity = pLocal->m_vecVelocity();
		s_iMaxTicks = 0;
	}
}

bool CTicks::ValidWeapon(CTFWeaponBase* pWeapon)
{
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_GRAPPLINGHOOK:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
		return false;
	}

	return true;
}

void CTicks::MoveFunc(float accumulated_extra_samples, bool bFinalTick)
{
	m_iShiftedTicks--;
	if (m_iWait > 0)
		m_iWait--;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	auto pWeapon = H::Entities.GetWeapon();
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		m_iWait = -1;

	m_bGoalReached = bFinalTick && m_iShiftedTicks == m_iShiftedGoal;

	static auto CL_Move = U::Hooks.m_mHooks["CL_Move"];
	CL_Move->Call<void>(accumulated_extra_samples, bFinalTick);
}

void CTicks::Move(float accumulated_extra_samples, bool bFinalTick)
{
	MoveManage();

	while (m_iShiftedTicks > m_iMaxShift)
		MoveFunc(accumulated_extra_samples, false);
	m_iShiftedTicks = std::max(m_iShiftedTicks, 0) + 1;

	if (m_bSpeedhack)
	{
		m_iShiftedTicks = Vars::Speedhack::Scale.Value;
		m_iShiftedGoal = 0;
	}

	m_iShiftedGoal = std::clamp(m_iShiftedGoal, 0, m_iMaxShift);
	if (m_iShiftedTicks > m_iShiftedGoal) // normal use/doubletap/teleport
	{
		m_iShiftStart = m_iShiftedTicks - 1;
		m_bShifted = false;

		while (m_iShiftedTicks > m_iShiftedGoal)
		{
			m_bShifting = m_bShifted |= m_iShiftedTicks - 1 != m_iShiftedGoal;
			MoveFunc(accumulated_extra_samples, m_iShiftedTicks - 1 == m_iShiftedGoal);
		}

		m_bShifting = m_bAntiWarp = m_bTimingUnsure = false;
		if (m_bWarp)
			m_iDeficit = 0;

		m_bDoubletap = m_bWarp = false;
	}
	else // else recharge, run once if we have any choked ticks
	{
		if (I::ClientState->chokedcommands)
			MoveFunc(accumulated_extra_samples, bFinalTick);
	}
}

void CTicks::MoveManage()
{
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	Recharge(pLocal);
	Warp();
	Speedhack();

	if (!m_bRecharge)
		m_iWait = std::max(m_iWait, 0);
	if (auto pWeapon = H::Entities.GetWeapon())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_PIPEBOMBLAUNCHER:
		case TF_WEAPON_CANNON:
			if (!G::CanSecondaryAttack)
				m_iWait = Vars::Doubletap::TickLimit.Value;
			break;
		default:
			if (!ValidWeapon(pWeapon))
				m_iWait = -1;
			else if (G::Attacking || !G::CanPrimaryAttack && !G::Reloading)
				m_iWait = Vars::Doubletap::TickLimit.Value;
		}
	}
	else
		m_iWait = -1;

	static auto sv_maxusrcmdprocessticks = H::ConVars.FindVar("sv_maxusrcmdprocessticks");
	m_iMaxUsrCmdProcessTicks = sv_maxusrcmdprocessticks->GetInt();
	if (F::AntiCheatCompatibility.Active())
		m_iMaxUsrCmdProcessTicks = std::min(m_iMaxUsrCmdProcessTicks, 8);
	m_iMaxShift = m_iMaxUsrCmdProcessTicks - std::max(m_iMaxUsrCmdProcessTicks - Vars::Doubletap::RechargeLimit.Value, 0) - (F::AntiAim.YawOn() ? F::AntiAim.AntiAimTicks() : 0);
	m_iMaxShift = std::max(m_iMaxShift, 1);
}

void CTicks::CreateMove(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket)
{
	Doubletap(pLocal, pCmd);
	AntiWarp(pLocal, pCmd);
	ManagePacket(pCmd, pSendPacket);

	SaveShootPos(pLocal);
	SaveShootAngle(pCmd, *pSendPacket);

	if (m_bDoubletap && m_iShiftedTicks == m_iShiftStart && pWeapon && pWeapon->IsInReload())
		m_bTimingUnsure = true;
}

void CTicks::ManagePacket(CUserCmd* pCmd, bool* pSendPacket)
{
	if (!m_bDoubletap && !m_bWarp && !m_bSpeedhack)
	{
		static bool bWasSet = false;
		bool bCanChoke = CanChoke(true); // failsafe
		if (G::PSilentAngles && bCanChoke)
			*pSendPacket = false, bWasSet = true;
		else if (bWasSet || !bCanChoke)
			*pSendPacket = true, bWasSet = false;

		bool bShouldShift = m_iShiftedTicks && m_iShiftedTicks + I::ClientState->chokedcommands >= m_iMaxUsrCmdProcessTicks;
		if (!*pSendPacket && bShouldShift)
			m_iShiftedGoal = std::max(m_iShiftedGoal - 1, 0);
	}
	else
	{
		if ((m_bSpeedhack || m_bWarp) && G::Attacking == 1)
		{
			*pSendPacket = true;
			return;
		}

		*pSendPacket = m_iShiftedGoal == m_iShiftedTicks;
		if (I::ClientState->chokedcommands >= 21) // prevent overchoking
			*pSendPacket = true;
	}
}

void CTicks::Start(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	Vec2 vOriginalMove; int iOriginalButtons;
	if (m_bPredictAntiwarp = m_bAntiWarp || GetTicks(H::Entities.GetWeapon()) && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
	{
		vOriginalMove = { pCmd->forwardmove, pCmd->sidemove };
		iOriginalButtons = pCmd->buttons;

		AntiWarp(pLocal, pCmd->viewangles.y, pCmd->forwardmove, pCmd->sidemove);
	}

	F::EnginePrediction.Start(pLocal, pCmd);

	if (m_bPredictAntiwarp)
	{
		pCmd->forwardmove = vOriginalMove.x, pCmd->sidemove = vOriginalMove.y;
		pCmd->buttons = iOriginalButtons;
	}
}

void CTicks::End(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (m_bPredictAntiwarp && !m_bAntiWarp && !G::Attacking)
	{
		F::EnginePrediction.End(pLocal, pCmd);
		F::EnginePrediction.Start(pLocal, pCmd);
	}
}

bool CTicks::CanChoke(bool bCanShift, int iMaxTicks)
{
	bool bCanChoke = I::ClientState->chokedcommands < 21;
	if (bCanChoke && !bCanShift)
		bCanChoke = m_iShiftedTicks + I::ClientState->chokedcommands < iMaxTicks;
	return bCanChoke;
}
bool CTicks::CanChoke(bool bCanShift)
{
	return CanChoke(bCanShift, m_iMaxUsrCmdProcessTicks);
}

int CTicks::GetTicks(CTFWeaponBase* pWeapon)
{
	if (m_bDoubletap && m_iShiftedGoal < m_iShiftedTicks)
		return m_iShiftedTicks - m_iShiftedGoal;

	if (!Vars::Doubletap::Doubletap.Value
		|| m_iWait || m_bWarp || m_bRecharge || m_bSpeedhack || F::AutoRocketJump.IsRunning())
		return 0;

	int iTicks = std::min(m_iShiftedTicks + 1, 22);
	if (!(iTicks >= Vars::Doubletap::TickLimit.Value || pWeapon && GetShotsWithinPacket(pWeapon, iTicks) > 1))
		return 0;
	
	return std::min(Vars::Doubletap::TickLimit.Value - 1, m_iMaxShift);
}

int CTicks::GetShotsWithinPacket(CTFWeaponBase* pWeapon, int iTicks)
{
	iTicks = std::min(m_iMaxShift + 1, iTicks);

	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return 1 + (iTicks - iDelay) / std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL);
}

int CTicks::GetMinimumTicksNeeded(CTFWeaponBase* pWeapon)
{
	int iDelay = 1;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_MINIGUN:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		iDelay = 2;
	}

	return (GetShotsWithinPacket(pWeapon) - 1) * std::ceilf(pWeapon->GetFireRate() / TICK_INTERVAL) + iDelay;
}

void CTicks::SaveShootPos(CTFPlayer* pLocal)
{
	if (m_iShiftedTicks == m_iShiftStart)
		m_vShootPos = pLocal->GetShootPos();
}
Vec3 CTicks::GetShootPos()
{
	return m_vShootPos;
}

void CTicks::SaveShootAngle(CUserCmd* pCmd, bool bSendPacket)
{
	static auto sv_maxusrcmdprocessticks_holdaim = H::ConVars.FindVar("sv_maxusrcmdprocessticks_holdaim");

	if (bSendPacket)
		m_vShootAngle = std::nullopt;
	else if (!m_vShootAngle && G::Attacking == 1 && sv_maxusrcmdprocessticks_holdaim->GetBool())
		m_vShootAngle = pCmd->viewangles;
}
Vec3* CTicks::GetShootAngle()
{
	if (m_vShootAngle && I::ClientState->chokedcommands)
		return &m_vShootAngle.value();
	return nullptr;
}

bool CTicks::IsTimingUnsure()
{	// actually knowing when we'll shoot would be better than this, but this is fine for now
	return m_bTimingUnsure || m_bSpeedhack /*|| m_bWarp*/;
}

void CTicks::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Ticks) || !pLocal->IsAlive())
		return;

	const DragBox_t dtPos = Vars::Menu::TicksDisplay.Value;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);

	// Speedhack indicator
	if (m_bSpeedhack)
	{
		H::Draw.StringOutlined(
			fFont,
			dtPos.x,
			dtPos.y + 2,
			Vars::Menu::Theme::Active.Value,
			Vars::Menu::Theme::Background.Value,
			ALIGN_TOP,
			std::format("Speedhack x{}", Vars::Speedhack::Scale.Value).c_str()
		);
		return;
	}

	// =========================================================
	// TICK CALCULATION
	// =========================================================

	int iAntiAimTicks = F::AntiAim.YawOn()
		? F::AntiAim.AntiAimTicks()
		: 0;

	int iTicks = std::clamp(
		m_iShiftedTicks +
		std::max(I::ClientState->chokedcommands - iAntiAimTicks, 0),
		0,
		m_iMaxUsrCmdProcessTicks
	);

	int iMax = std::max(
		m_iMaxUsrCmdProcessTicks - iAntiAimTicks,
		0
	);

	// Prevent division by zero
	float flRatio = iMax > 0
		? std::clamp(
			static_cast<float>(iTicks) / static_cast<float>(iMax),
			0.0f,
			1.0f
		)
		: 0.0f;

	// =========================================================
	// DIMENSIONS
	// Same general proportions as CritHack
	// =========================================================

	constexpr int PANEL_WIDTH = 180;

	const int scaledWidth =
		H::Draw.Scale(PANEL_WIDTH, Scale_Round);

	const int panelX =
		dtPos.x - scaledWidth / 2;

	const int panelY =
		dtPos.y - H::Draw.Scale(6, Scale_Round);

	const int barHeight =
		H::Draw.Scale(14, Scale_Round);

	const int barY =
		panelY +
		fFont.m_nTall +
		H::Draw.Scale(4, Scale_Round);

	const int barWidth = scaledWidth;
	const int barX = panelX;

	// =========================================================
	// STATUS TEXT
	// =========================================================

	std::string sStatusText;

	if (m_iWait)
	{
		sStatusText = "⚡ TICKS NOT READY";
	}
	else if (iMax > 0 && iTicks >= iMax)
	{
		sStatusText = "⚡ TICKS READY";
	}
	else
	{
		sStatusText = "⚡ BUILDING TICKS";
	}

	H::Draw.String(
		fFont,
		dtPos.x,
		panelY,
		Vars::Menu::Theme::Active.Value,
		ALIGN_TOP,
		sStatusText.c_str()
	);

	// =========================================================
	// GLASS BAR BACKGROUND
	// =========================================================

	H::Draw.FillRect(
		barX,
		barY,
		barWidth,
		barHeight,
		Color_t(10, 15, 25, 180)
	);

	// Inner dark glass
	H::Draw.FillRect(
		barX + H::Draw.Scale(1, Scale_Round),
		barY + H::Draw.Scale(1, Scale_Round),
		barWidth - H::Draw.Scale(2, Scale_Round),
		barHeight - H::Draw.Scale(2, Scale_Round),
		Color_t(0, 0, 0, 100)
	);

	// =========================================================
	// PROGRESS FILL
	// =========================================================

	if (flRatio > 0.0f)
	{
		const int fillWidth =
			static_cast<int>(
				(barWidth - H::Draw.Scale(2, Scale_Round))
				* flRatio
				);

		const int fillX =
			barX + H::Draw.Scale(1, Scale_Round);

		const int fillY =
			barY + H::Draw.Scale(1, Scale_Round);

		const int fillH =
			barHeight - H::Draw.Scale(2, Scale_Round);

		Color_t fillColor =
			Vars::Menu::Theme::Accent.Value;

		fillColor.a =
			180 + static_cast<int>(flRatio * 75);

		// Main fill
		H::Draw.FillRect(
			fillX,
			fillY,
			fillWidth,
			fillH,
			fillColor
		);

		// =====================================================
		// GLASS SHINE
		// =====================================================

		H::Draw.FillRect(
			fillX,
			fillY,
			fillWidth,
			fillH / 2,
			Color_t(
				255,
				255,
				255,
				30 + static_cast<int>(flRatio * 40)
			)
		);

		// =====================================================
		// FILL EDGE HIGHLIGHT
		// =====================================================

		if (flRatio < 0.95f &&
			fillWidth > H::Draw.Scale(10, Scale_Round))
		{
			H::Draw.FillRect(
				fillX + fillWidth - H::Draw.Scale(2, Scale_Round),
				fillY,
				H::Draw.Scale(2, Scale_Round),
				fillH,
				Color_t(
					255,
					255,
					255,
					40 + static_cast<int>(flRatio * 50)
				)
			);
		}
	}

	// =========================================================
	// BAR BORDER
	// =========================================================

	H::Draw.LineRect(
		barX,
		barY,
		barWidth,
		barHeight,
		Color_t(
			255,
			255,
			255,
			15 + static_cast<int>(flRatio * 30)
		)
	);

	// =========================================================
	// READY PULSE
	// Same style as CritHack
	// =========================================================

	if (!m_iWait && iMax > 0 && iTicks >= iMax)
	{
		const float pulse =
			(sinf(I::GlobalVars->curtime * 3.0f) + 1.0f) / 2.0f;

		const int pulseAlpha =
			20 + static_cast<int>(pulse * 60);

		Color_t pulseColor =
			Vars::Menu::Theme::Accent.Value;

		pulseColor.a = pulseAlpha;

		// Outer glow
		H::Draw.FillRect(
			barX - H::Draw.Scale(2, Scale_Round),
			barY - H::Draw.Scale(2, Scale_Round),
			barWidth + H::Draw.Scale(4, Scale_Round),
			barHeight + H::Draw.Scale(4, Scale_Round),
			pulseColor
		);

		// Inner glow
		Color_t innerPulse =
			Vars::Menu::Theme::Accent.Value;

		innerPulse.a = pulseAlpha / 2;

		H::Draw.FillRect(
			barX,
			barY - H::Draw.Scale(1, Scale_Round),
			barWidth,
			barHeight + H::Draw.Scale(2, Scale_Round),
			innerPulse
		);
	}

	// =========================================================
	// TICK COUNT
	// =========================================================

	std::string sTickCount =
		std::to_string(iTicks) +
		" / " +
		std::to_string(iMax) +
		" ticks";

	H::Draw.String(
		fFont,
		dtPos.x,
		barY + barHeight + H::Draw.Scale(4, Scale_Round),
		m_iWait
		? Color_t(255, 255, 255, 180)
		: Color_t(255, 255, 255, 200),
		ALIGN_TOP,
		sTickCount.c_str()
	);

	// =========================================================
	// TICK MARKS
	// =========================================================

	{
		const int tickMarkCount =
			std::min(8, iMax);

		if (tickMarkCount > 1)
		{
			const float tickSpacing =
				static_cast<float>(
					barWidth - H::Draw.Scale(2, Scale_Round)
					) / tickMarkCount;

			const int tickY =
				barY + H::Draw.Scale(2, Scale_Round);

			const int tickH =
				barHeight - H::Draw.Scale(4, Scale_Round);

			for (int i = 1; i < tickMarkCount; i++)
			{
				const int tickX =
					barX +
					H::Draw.Scale(1, Scale_Round) +
					static_cast<int>(i * tickSpacing);

				const bool isFilled =
					static_cast<float>(i) / tickMarkCount <= flRatio;

				H::Draw.Line(
					tickX,
					tickY,
					tickX,
					tickY + tickH,
					Color_t(
						255,
						255,
						255,
						isFilled
						? 20 + static_cast<int>(flRatio * 30)
						: 8
					)
				);
			}
		}
	}
}
