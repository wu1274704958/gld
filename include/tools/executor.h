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
			push(Task(f));
		}

		void delay(std::function<void()> f, int ms)
		{
			auto end = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() ).count() + ms;
			push(Task(f,TaskType::DelayMS,end));
		}

		void delay_frame(std::function<void()> f, int frame)
		{
			push(Task(f, TaskType::DelayFrame, frame));
		}

		void push(Task t)
		{
			if (mtx.try_lock())
			{
				que.push_back(std::move(t));
				mtx.unlock();
			}
			else {
				std::lock_guard<std::mutex> guard(temp_mtx);
				temp_que.push_back(std::move(t));
			}
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
				}
				else if (it->ty == TaskType::DelayMS)
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
			std::lock_guard<std::mutex> t_guard(temp_mtx);
			if (!temp_que.empty())
			{
				for (auto& t : temp_que)
					que.push_back(std::move(t));
				temp_que.clear();
			}
				
		}
	protected:
		std::vector<Task> que;
		std::vector<Task> temp_que;
		std::mutex mtx,temp_mtx;
	};
}