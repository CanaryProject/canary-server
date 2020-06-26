
/*
extern Actions* g_actions; // 26 - 17
extern Chat* g_chat; // 33 - 26
extern ConfigManager g_config; // 193 - 164
extern CreatureEvents* g_creatureEvents; // 28 - 18
*extern Database g_database; // 144
extern DatabaseTasks g_databaseTasks; // 22 - 20
extern Decay g_decay; // 5 - 3
extern Dispatcher g_dispatcher; // 47 - 44
extern Events* g_events; // 45 - 36
*extern Game g_game; // 670
extern GlobalEvents* g_globalEvents; // 18 - 10
*extern LuaEnvironment g_luaEnvironment; // 43 - 37
extern Modules g_modules; // 7 - 4
extern Monsters g_monsters; // 32 - 24
extern MoveEvents* g_moveEvents; // 28 - 18
extern RSA g_RSA; // 5 - 3
extern Scheduler g_scheduler; // 62 - 59
extern Scripts* g_scripts; // 12 - 6
extern Spells* g_spells; // 38 - 26
extern TalkActions* g_talkActions; // 16 - 9
extern Vocations g_vocations; // 32 - 24
extern Weapons* g_weapons; // 30 - 18
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