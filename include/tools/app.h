#pragma once 

#include <tools/executor.h>
#include "tools/tween.hpp"
#include <memory>
#include <mutex>

namespace gld {

	struct App {

		App() : tween(exec)
		{

		}

		Executor exec;
		Tween tween;

		static std::shared_ptr<App> self;
		static std::mutex mtx;
		static std::shared_ptr<App> instance();
	};
}