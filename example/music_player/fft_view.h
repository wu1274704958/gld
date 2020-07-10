
#pragma once

#include <node.hpp>
#include <component.h>

namespace gld {

	struct FFTView : public Node<Component> {
		virtual void on_update(float* data, int len) = 0;
		virtual int fft_data_length() = 0;
	};

}