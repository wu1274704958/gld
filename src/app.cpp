#include <tools/app.h>

namespace gld{
	std::shared_ptr<App> App::self;
	std::mutex App::mtx;
	std::shared_ptr<App> App::instance()
	{
		mtx.lock();
		if (!self) {
			self = std::shared_ptr<App>(new App());
		}
		mtx.unlock();
		return self;
	}
}