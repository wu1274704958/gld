#pragma once 

#include <functional>
#include <vector>
#include <mutex>
#include <chrono>

namespace gld {

	enum class TaskType : uint8_t {
		NextFrame = 0x0,
		DelayFrame,
		DelayMS
	};

	struct Task {
		
		Task(std::function<void()> func):func(func){}
		Task(std::function<void()> func, TaskType ty, uint64_t val) :func(func),ty(ty),val(val) {}
		uint64_t val = 0;
		std::function<void()> func;
		TaskType ty = TaskType::NextFrame;
	};

	struct Executor {
		

		void delay(std::function<void()> f)
		{
			std::lock_guard<std::mutex> guard(mtx);
			que.push_back(Task(f));
		}

		void delay(std::function<void()> f, int ms)
		{
			auto end = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() ).count() + ms;
			std::lock_guard<std::mutex> guard(mtx);
			que.push_back(Task(f,TaskType::DelayMS,end));
		}

		void delay_frame(std::function<void()> f, int frame)
		{
			std::lock_guard<std::mutex> guard(mtx);
			que.push_back(Task(f, TaskType::DelayFrame, frame));
		}

		void do_loop()
		{
			std::lock_guard<std::mutex> guard(mtx);
			for (auto it = que.begin(); it != que.end();)
			{
				if (it->ty == TaskType::NextFrame)
				{
					(it->func)();
					it = que.erase(it);
					continue;
				}
				else if (it->ty == TaskType::DelayFrame)
				{
					if (it->val == 0)
					{
						(it->func)();
						it = que.erase(it);
						continue;
					}
				}else if(it->ty == TaskType::DelayMS)
				{
					auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					if (now >= it->val)
					{
						(it->func)();
						it = que.erase(it);
						continue;
					}
				}
				++it;
			}
		}
	protected:
		std::vector<Task> que;
		std::mutex mtx;
	};
}