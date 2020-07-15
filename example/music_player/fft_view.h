
#pragma once

#include <node.hpp>
#include <component.h>

namespace gld {

	struct FFTView : public Node<Component> {

		using FFT_VAL_TYPE = std::tuple<
			Vmv<static_cast<size_t>(BASS_DATA_FFT256 ), 128>,
			Vmv<static_cast<size_t>(BASS_DATA_FFT512 ), 256>,
			Vmv<static_cast<size_t>(BASS_DATA_FFT1024), 512>,
			Vmv<static_cast<size_t>(BASS_DATA_FFT2048), 1024>,
			Vmv<static_cast<size_t>(BASS_DATA_FFT4096), 2048>>;

		virtual void on_update(float* data, int len) = 0;
		virtual size_t fft_data_length() = 0;
	};

}