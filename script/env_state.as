const int STATE_OFF = 0;
const int STATE_ON = 1;
const int SF_ENVSTATE_START_ON = 1;

class env_state : ScriptBaseEntity
{
	int m_iState;
	string m_fireWhenOn;
	string m_fireWhenOff;

	void SetState(int iState)
	{
		m_iState = iState;
		pev.iuser3 = iState + 1;
	}

	void Spawn()
	{
		SetState(((pev.spawnflags & SF_ENVSTATE_START_ON) != 0) ? STATE_ON : STATE_OFF);
		m_fireWhenOn = pev.noise1;
		pev.noise1 = '';
		m_fireWhenOff = pev.noise2;
		pev.noise2 = '';
	}

	void Use( CBaseEntity @pActivator, CBaseEntity @pCaller, USE_TYPE useType, float value )
	{
		if (!self.ShouldToggle(useType, m_iState == STATE_ON))
			return;

		switch (m_iState)
		{
		case STATE_ON:
			SetState(STATE_OFF);
			g_EntityFuncs.FireTargets(pev.target, pActivator, self, USE_OFF);
			g_EntityFuncs.FireTargets(m_fireWhenOff, pActivator, self, USE_TOGGLE);
			break;
		case STATE_OFF:
			SetState(STATE_ON);
			g_EntityFuncs.FireTargets(pev.target, pActivator, self, USE_ON);
			g_EntityFuncs.FireTargets(m_fireWhenOn, pActivator, self, USE_TOGGLE);
			break;
		}
	}

	int ObjectCaps() { return BaseClass.ObjectCaps() | FCAP_MASTER; }

	bool IsTriggered(CBaseEntity@ pActivator) { return m_iState == STATE_ON; }
}

// ---- TEST ONLY: remove before shipping ----
void MapInit()
{
	g_CustomEntityFuncs.RegisterCustomEntity( "env_state", "env_state" );
}
// ---- END TEST ONLY ----
