#include "MusicPlayer.h"
#include <bassflac.h>
#include "MMFile.h"

#ifndef PF_WIN32 
#define WCHAR void
#endif

fv::MusicPlayer::MusicPlayer(bool enable3d)
{
	if (enable3d)
	{
		if (!BASS_Init(-1, 44100, BASS_DEVICE_3D, NULL, NULL))
		{
			throw "BASS init failed";
		}
		BASS_Set3DFactors(1, 1, 1);
	}
	else
	{
		if (!BASS_Init(-1, 44100, 0, NULL, NULL))
		{
			init_error_code = BASS_ErrorGetCode();
			//throw "BASS init failed";
		}
		
	}
	IsEnable3D = enable3d;
}


fv::MusicPlayer::~MusicPlayer()
{
	cleanup();
	BASS_Free();
}

int fv::MusicPlayer::playStream(const  MMFile& file, bool loop)
{
	if (init_error_code != 0)
		return init_error_code;
	if (chan)
	{
		if (BASS_ChannelIsActive(chan) != BASS_ACTIVE_STOPPED)
		{
			BASS_ChannelStop(chan);
		}
		BASS_SampleFree(chan);
	}
	/*if(lstrcmpW(file.getSuffix(),L".flac") == 0)
	{ 
		if (IsEnable3D)
		{
			chan = BASS_FLAC_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, BASS_MUSIC_FLOAT | BASS_SAMPLE_3D | BASS_SAMPLE_MONO);
			IsSupport3D = true;
			if (!chan && BASS_ErrorGetCode() == BASS_ERROR_NO3D)
			{
				chan = BASS_FLAC_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, BASS_MUSIC_FLOAT | BASS_SAMPLE_MONO);
				IsSupport3D = false;
			}
		}
		else
		{
			chan = BASS_FLAC_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, BASS_MUSIC_FLOAT| BASS_SAMPLE_MONO);
		}	
	}
	else*/
	{
		if (IsEnable3D)
		{
			chan = BASS_SampleLoad(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, 1,BASS_MUSIC_FLOAT | BASS_SAMPLE_3D | BASS_MUSIC_MONO );
			IsSupport3D = true;
			if (!chan && BASS_ErrorGetCode() == BASS_ERROR_NO3D)
			{
				if (lstrcmpW(file.getSuffix(), MMFILE_FLAC_LIT_STR) == 0)
				{
					chan = BASS_FLAC_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, 0);
				}
				else {
					chan = BASS_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, 0);
				}
				
				IsSupport3D = false;
			}
		}
		else
		{
			if (lstrcmpW(file.getSuffix(), MMFILE_FLAC_LIT_STR) == 0)
			{
				chan = BASS_FLAC_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, 0);
			}
			else {
				chan = BASS_StreamCreateFile(FALSE, (const WCHAR *)file.getAbsolutePath(), 0, 0, 0);
			}
		}
	}
	if (chan)
	{
		if (IsEnable3D && IsSupport3D)
			BASS_SampleGetChannel(chan, FALSE);
		chan_max_len = BASS_ChannelGetLength(chan, BASS_POS_BYTE);
		BASS_ChannelPlay(chan, loop);
	}
	else
	{
		printf("%d \n", BASS_ErrorGetCode());
	}
	return 0;
}

void fv::MusicPlayer::play(bool loop)
{
	if (chan)
	{
		if (BASS_ChannelIsActive(chan) != BASS_ACTIVE_PLAYING && BASS_ChannelIsActive(chan) != BASS_ACTIVE_STOPPED)
		{
			BASS_ChannelPlay(chan, loop);
		}
	}
}

void fv::MusicPlayer::pause()
{
	if (chan)
	{
		if (BASS_ChannelIsActive(chan) != BASS_ACTIVE_PAUSED && BASS_ChannelIsActive(chan) != BASS_ACTIVE_STOPPED)
		{
			BASS_ChannelPause(chan);
		}
	}
}

void fv::MusicPlayer::stop()
{
	if (chan)
	{
		if (BASS_ChannelIsActive(chan) != BASS_ACTIVE_STOPPED)
		{
			BASS_ChannelStop(chan);
		}
	}
}

size_t fv::MusicPlayer::getData(void *p, size_t size)
{
	if(chan)
	{
		return BASS_ChannelGetData(chan, p, size);
	}
}

DWORD fv::MusicPlayer::getLevel()
{
	return BASS_ChannelGetLevel(chan);
}

QWORD fv::MusicPlayer::getPosition(int flag)
{
	return BASS_ChannelGetPosition(chan, flag);
}

void fv::MusicPlayer::setPosition(QWORD pos, int flag)
{
	BASS_ChannelSetPosition(chan, pos, flag);
}

int fv::MusicPlayer::getActive()
{
	return BASS_ChannelIsActive(chan);
}

void fv::MusicPlayer::cleanup()
{
	if (chan)
	{
		if (IsEnable3D && IsSupport3D)
			BASS_SampleFree(chan);
		else
			BASS_StreamFree(chan);
		chan = 0;
	}
}

QWORD fv::MusicPlayer::getLength()
{
	return chan_max_len;
}

bool fv::MusicPlayer::isOff()
{
	if (chan)
		return false;
	else
		return true;
}

HSAMPLE fv::MusicPlayer::getChan()
{
	return chan;
}

bool fv::MusicPlayer::isEnable3D()
{
	return IsEnable3D;
}

bool fv::MusicPlayer::isSupport3D()
{
	return IsSupport3D;
}

int fv::MusicPlayer::get_init_err_code()
{
	return init_error_code;
}



