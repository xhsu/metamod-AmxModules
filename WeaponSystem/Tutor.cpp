
import std;
import hlsdk;

struct CBaseTutorState
{
	int (**_vptr_CBaseTutorState)(...);

	int m_type;
};

struct CBaseTutorStateSystem
{
	int (**_vptr_CBaseTutorStateSystem)(...);

	CBaseTutorState* m_currentState;
};

struct TutorMessageEventParam
{
	char* m_data;
	TutorMessageEventParam* m_next;
};

struct TutorMessageEvent
{
	int (**_vptr_TutorMessageEvent)(...);

	int m_messageID;
	int m_duplicateID;
	float m_activationTime;
	float m_lifetime;
	int m_priority;
	int m_numParameters;
	TutorMessageEventParam* m_paramList;
	TutorMessageEvent* m_next;
};

struct CBaseTutor
{
	int (**_vptr_CBaseTutor)(...);

	CBaseTutorStateSystem* m_stateSystem;
	TutorMessageEvent* m_eventList;
	float m_deadAirStartTime;
	float m_roundStartTime;
};

enum TutorMessageKeepOldType : __int32
{
	TUTORMESSAGEKEEPOLDTYPE_DONT_KEEP_OLD = 0x0,
	TUTORMESSAGEKEEPOLDTYPE_KEEP_OLD = 0x1,
	TUTORMESSAGEKEEPOLDTYPE_UPDATE_CONTENT = 0x2,
};

enum TutorMessageClass : __int32
{
	TUTORMESSAGECLASS_NORMAL = 0x0,
	TUTORMESSAGECLASS_EXAMINE = 0x1,
};

enum TutorMessageType : __int32
{
	TUTORMESSAGETYPE_DEFAULT = 0x1,
	TUTORMESSAGETYPE_FRIEND_DEATH = 0x2,
	TUTORMESSAGETYPE_ENEMY_DEATH = 0x4,
	TUTORMESSAGETYPE_SCENARIO = 0x8,
	TUTORMESSAGETYPE_BUY = 0x10,
	TUTORMESSAGETYPE_CAREER = 0x20,
	TUTORMESSAGETYPE_HINT = 0x40,
	TUTORMESSAGETYPE_INGAME_HINT = 0x80,
	TUTORMESSAGETYPE_END_GAME = 0x100,
	TUTORMESSAGETYPE_LAST = 0x101,
	TUTORMESSAGETYPE_ALL = 0x1FF,
};

enum TutorMessageInterruptFlag : __int32
{
	TUTORMESSAGEINTERRUPTFLAG_DEFAULT = 0x0,
	TUTORMESSAGEINTERRUPTFLAG_NOW_DAMMIT = 0x1,
};

struct TutorMessage
{
	char* m_text;
	unsigned __int8 m_priority;
	unsigned __int8 m_duration;
	TutorMessageKeepOldType m_keepOld;
	TutorMessageClass m_class;
	unsigned __int8 m_decay;
	TutorMessageType m_type;
	int m_lifetime;
	TutorMessageInterruptFlag m_interruptFlag;
	int m_duplicateID;
	float m_examineStartTime;
	int m_timesShown;
	float m_minDisplayTimeOverride;
	float m_minRepeatInterval;
	float m_lastCloseTime;
};

typedef std::map<std::string, TutorMessage*> TutorMessageMap;

enum TutorMessageID
{
	YOU_FIRED_A_SHOT,
	YOU_SHOULD_RELOAD,
	YOU_ARE_OUT_OF_AMMO,
	YOU_KILLED_A_TEAMMATE,
	YOU_KILLED_PLAYER,
	YOU_KILLED_PLAYER_ONE_LEFT,
	YOU_KILLED_LAST_ENEMY,
	YOU_KILLED_PLAYER_HEADSHOT,
	YOU_KILLED_PLAYER_HEADSHOT_ONE_LEFT,
	YOU_KILLED_LAST_ENEMY_HEADSHOT,
	YOU_DIED,
	YOU_DIED_HEADSHOT,
	YOU_FELL_TO_YOUR_DEATH,
	YOU_WERE_JUST_HURT,
	YOU_ARE_BLIND_FROM_FLASHBANG,
	YOU_ATTACKED_TEAMMATE,
	BUY_TIME_BEGIN,
	BOMB_PLANTED_T,
	BOMB_PLANTED_CT,
	TEAMMATE_KILLED,
	TEAMMATE_KILLED_ONE_LEFT,
	LAST_TEAMMATE_KILLED,
	ENEMY_KILLED,
	ENEMY_KILLED_ONE_LEFT,
	LAST_ENEMY_KILLED,
	YOU_SPAWNED,
	YOU_SEE_FRIEND,
	YOU_SEE_ENEMY,
	YOU_SEE_FRIEND_CORPSE,
	YOU_SEE_ENEMY_CORPSE,
	YOU_SEE_LOOSE_BOMB_T,
	YOU_SEE_LOOSE_BOMB_CT,
	YOU_SEE_BOMB_CARRIER_T,
	YOU_SEE_BOMB_CARRIER_CT,
	YOU_SEE_PLANTED_BOMB_T,
	YOU_SEE_PLANTED_BOMB_CT,
	YOU_ARE_BOMB_CARRIER,
	YOU_SEE_LOOSE_WEAPON,
	YOU_SEE_LOOSE_DEFUSER,
	YOU_SEE_BOMBSITE_T,
	YOU_SEE_BOMBSITE_CT,
	YOU_SEE_BOMBSITE_T_BOMB,
	YOU_SEE_HOSTAGE_T,
	YOU_SEE_HOSTAGE_CT,
	YOU_SEE_HOSTAGE_CT_EXAMINE,
	YOU_USED_HOSTAGE_MORE_LEFT,
	YOU_USED_HOSTAGE_NO_MORE_LEFT,
	ALL_HOSTAGES_FOLLOWING_T,
	ALL_HOSTAGES_FOLLOWING_CT,
	HOSTAGE_RESCUED_T,
	HOSTAGE_RESCUED_CT,
	YOU_RESCUED_HOSTAGE,
	YOU_ARE_IN_BOMBSITE_T,
	YOU_ARE_IN_BOMBSITE_CT,
	YOU_ARE_IN_BOMBSITE_T_BOMB,
	ALL_HOSTAGES_RESCUED_T,
	ALL_HOSTAGES_RESCUED_CT,
	YOU_DAMAGED_HOSTAGE,
	YOU_KILLED_HOSTAGE,
	ALL_HOSTAGES_DEAD,
	YOU_HAVE_BEEN_SHOT_AT,
	TIME_RUNNING_OUT_DE_T,
	TIME_RUNNING_OUT_DE_CT,
	TIME_RUNNING_OUT_CS_T,
	TIME_RUNNING_OUT_CS_CT,
	DEFUSING_WITHOUT_KIT,
	BOMB_DEFUSED_T,
	BOMB_DEFUSED_CT,
	YOU_DEFUSED_BOMB,
	BOMB_EXPLODED_T,
	BOMB_EXPLODED_CT,
	ROUND_START_DE_T,
	ROUND_START_DE_CT,
	ROUND_START_CS_T,
	ROUND_START_CS_CT,
	ROUND_OVER,
	ROUND_DRAW,
	CT_WIN,
	T_WIN,
	DEATH_CAMERA_START,
	RADIO_COVER_ME,
	RADIO_YOU_TAKE_THE_POINT,
	RADIO_HOLD_THIS_POSITION,
	RADIO_REGROUP_TEAM,
	RADIO_FOLLOW_ME,
	RADIO_TAKING_FIRE,
	RADIO_GO_GO_GO,
	RADIO_TEAM_FALL_BACK,
	RADIO_STICK_TOGETHER_TEAM,
	RADIO_GET_IN_POSITION_AND_WAIT,
	RADIO_STORM_THE_FRONT,
	RADIO_REPORT_IN_TEAM,
	RADIO_AFFIRMATIVE,
	RADIO_ENEMY_SPOTTED,
	RADIO_NEED_BACKUP,
	RADIO_SECTOR_CLEAR,
	RADIO_IN_POSITION,
	RADIO_REPORTING_IN,
	RADIO_GET_OUT_OF_THERE,
	RADIO_NEGATIVE,
	RADIO_ENEMY_DOWN,
	BUY_NEED_PRIMARY,
	BUY_NEED_PRIMARY_AMMO,
	BUY_NEED_SECONDARY_AMMO,
	BUY_NEED_ARMOR,
	BUY_NEED_DEFUSE_KIT,
	BUY_NEED_GRENADE,
	CAREER_TASK_DONE_MORE_LEFT,
	CAREER_TASK_DONE_ONE_LEFT,
	CAREER_TASK_DONE_ALL_DONE,
	HINT_BEGIN,
	HINT_1,
	HINT_2,
	HINT_3,
	HINT_4,
	HINT_5,
	HINT_10,
	HINT_11,
	HINT_12,
	HINT_13,
	HINT_14,
	HINT_15,
	HINT_20,
	HINT_21,
	HINT_22,
	HINT_23,
	HINT_24,
	HINT_25,
	HINT_26,
	HINT_30,
	HINT_31,
	HINT_32,
	HINT_33,
	HINT_34,
	HINT_40,
	HINT_50,
	HINT_51,
	HINT_52,
	HINT_53,
	HINT_BOMB_START = 139,
	HINT_60 = 139,
	HINT_61 = 140,
	HINT_BOMB_END = 140,
	HINT_HOSTAGE_START = 141,
	HINT_70 = 141,
	HINT_71,
	HINT_72,
	HINT_73 = 144,
	HINT_HOSTAGE_END = 144,
	HINT_END,
	INGAME_HINT_BEGIN,
	INGAME_HINT_1,
	INGAME_HINT_2,
	INGAME_HINT_END,
	TUTOR_NUM_MESSAGES
};

struct ClientCorpseStruct
{
	Vector m_position;
	int m_team;
};

typedef std::vector<ClientCorpseStruct*> ClientCorpseList;

struct CCSTutor : CBaseTutor
{
	float m_nextViewableCheckTime;
	TutorMessageMap m_messageMap;
	TutorMessageID m_currentlyShownMessageID;
	float m_currentlyShownMessageCloseTime;
	float m_currentlyShownMessageStartTime;
	float m_currentlyShownMessageMinimumCloseTime;
	TutorMessageEvent* m_currentMessageEvent;
	TutorMessageEvent* m_lastScenarioEvent;
	TutorMessageID m_lastHintShown;
	TutorMessageID m_lastInGameHintShown;
	ClientCorpseList m_clientCorpseList;
	int m_messageTypeMask;
	bool m_haveSpawned;

	struct PlayerDeathStruct
	{
		bool m_hasBeenShown;
		TutorMessageEvent* m_event;
	}
	m_playerDeathInfo[32];
};
