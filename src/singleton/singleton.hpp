
/*
extern Actions* g_actions; // 26
extern Chat* g_chat; // 33
extern ConfigManager g_config; // 193
extern CreatureEvents* g_creatureEvents; // 28
extern Database g_database; // 144
extern DatabaseTasks g_databaseTasks; // 22
extern Dispatcher g_dispatcher; // 47
extern Events* g_events; // 45
extern Game g_game; // 670
extern GlobalEvents* g_globalEvents; // 18
extern LuaEnvironment g_luaEnvironment; // 43
extern Modules g_modules; // 7
extern Monsters g_monsters; // 32
extern MoveEvents* g_moveEvents; // 28
extern RSA g_RSA; // 5
extern Scheduler g_scheduler; // 62
extern Scripts* g_scripts; // 12
extern Spells* g_spells; // 38
extern TalkActions* g_talkActions; // 16
extern Vocations g_vocations; // 32
extern Weapons* g_weapons; // 30
*/

/*
extern GlobalEvents* g_globalEvents; // 10
extern Decay g_decay; // 3
extern Modules g_modules; // 4
extern RSA g_RSA; // 3
extern Scripts* g_scripts; // 6
*/

class Sample
{
	public:
		// Singleton - ensures we don't accidentally copy it
		Decay(Decay const&) = delete;
		void operator=(Decay const&) = delete;

		static Decay& getInstance() {
			static Decay instance; // Guaranteed to be destroyed.
														// Instantiated on first use.
			return instance;
		}

	private:
		Decay() {}
};

constexpr auto g_decay = &Decay::getInstance;