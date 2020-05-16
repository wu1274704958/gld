#pragma once

#include <iostream>
#include <filesystem>
#include <functional>

namespace wws {

#ifndef PF_ANDROID

	std::tuple<bool,std::vector<std::filesystem::path>> find_paths_ex(std::filesystem::path& dir, std::function<bool(const std::filesystem::path&)> interest)
	{
		std::vector<std::filesystem::path> res;
		std::vector<std::filesystem::path> res_all;
		if (std::filesystem::exists(dir))
		{
			for (auto& it : std::filesystem::directory_iterator(dir))
			{
				if (interest(it))
				{
					res.push_back(it);
				}else{
					res_all.push_back(it);
				}
			}
		}
		if (!res.empty())
			return std::make_tuple(true, res);
		else
			return std::make_tuple(false, res_all);
	}

    std::filesystem::path find_path(int uplimit,const char* name,bool is_dir = false)
    {
    	std::filesystem::path res;
    	std::vector<std::filesystem::path> wait_ck;
		std::filesystem::path previous;
    	std::filesystem::path root(".");
    	root = std::filesystem::absolute(root);
    	wait_ck.push_back(root);

    	auto f = [name,is_dir](const std::filesystem::path& t)->bool {
			if(is_dir)
    			return std::filesystem::is_directory(t) && t.filename() == name;
			else 
				return std::filesystem::is_regular_file(t) && t.filename() == name;
    	};

    	while (uplimit > 0)
    	{
    		auto p = std::move(wait_ck.back());
    		wait_ck.pop_back();
    		auto [finded, ps] = find_paths_ex(p, f);
    		if (finded)
    		{
			    res = std::move(ps[0]);
			    goto FINDED;
    		}
    		else {
    			for (auto& p : ps)
    			{
    				if (std::filesystem::is_directory(p) && previous != p)
    					wait_ck.push_back(std::move(p));
    			}
    		}
    		if (wait_ck.empty())
    		{
    			if (!root.has_parent_path())
    				break;
				previous = root;
    			root = root.parent_path();
    			wait_ck.push_back(root);
    			--uplimit;
    		}
    	}
    	FINDED:
    	return res;
    }

	void enum_path(std::filesystem::path& root,std::function<void(const std::filesystem::path&)> ob,bool only_file = true)
    {
    	std::filesystem::path res;
    	std::vector<std::filesystem::path> wait_ck;
    	wait_ck.push_back(root);

    	auto f = [&ob,only_file](const std::filesystem::path& t)->bool {
			if(only_file)
			{
				if(std::filesystem::is_regular_file(t))
    				ob(t);
			}
			else 
				ob(t);
			return false;
    	};

    	while (!wait_ck.empty())
    	{
    		auto p = std::move(wait_ck.back());
    		wait_ck.pop_back();
    		auto [finded, ps] = find_paths_ex(p, f);
    		if (finded)
    		{
			    res = std::move(ps[0]);
			    break;
    		}
    		else {
    			for (auto& p : ps)
    			{
    				if (std::filesystem::is_directory(p))
    					wait_ck.push_back(std::move(p));
    			}
    		}
    	}
    }

	#endif
}