#pragma once
#include <bass.h>
#include"MMFile.h"

namespace fv {
	class MusicPlayer
	{

	public:
		MusicPlayer(bool enable3d = false);
		~MusicPlayer();

		int playStream(const  MMFile& file, bool loop = false);
		void play(bool loop = false);
		void pause();
		void stop();
		size_t getData(void *p, size_t size);
		DWORD getLevel();
		QWORD getPosition(int flag);
		void setPosition(QWORD pos, int flag);
		double getSeconds();
		int getActive();
		void cleanup();
		QWORD getLength();
		bool isOff();


		HSAMPLE getChan();
		bool isEnable3D();
		bool isSupport3D();

		int get_init_err_code();

	protected:
		HSTREAM chan = 0;
		QWORD chan_max_len = 0;
		int init_error_code = 0;
		bool IsEnable3D = false;
		bool IsSupport3D = false;
	};

}
