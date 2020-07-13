#pragma once

#include <memory>
#include <vector>
#include <stack>
#include <functional>
#include "MMFile.h"
#include "MusicPlayer.h"


namespace fv {
	class Pumper
	{
	public:
		enum PUMP_MODE
		{
			NONE = 1,
			LOOP,
			RAND
		};

		Pumper( MusicPlayer &player);
		~Pumper();

		void pump();
		PUMP_MODE getMode();
		void setMode(PUMP_MODE mo);
		int getIndex();
		void setIndex(int ind);
		bool getPumpDir();
		void setPumpDir(bool pump_dir);
		void setFillMusicFunc(std::function<void(const std::shared_ptr<std::vector<MMFile>>&)> f);
		void setNextMusic(const MMFile *nm);
		
		void init(const char* root_dir);
		std::shared_ptr<std::vector<MMFile>> pop();
		void onclick(int idx);

		std::function<void(const MMFile&)> onAutoPlay;

	private:
		void rand();
		void loop();
		void none();
		void cleanup();

		template<size_t N>
		void all_templet();

		std::function<void(const std::shared_ptr<std::vector<MMFile>>&)> fill_music_func;

		PUMP_MODE m_mode;
		std::stack<std::shared_ptr<std::vector<MMFile>>> m_root;
		MusicPlayer &m_player;
		int m_index;
		bool pump_dir;
		const MMFile *next_music;
	};
}
