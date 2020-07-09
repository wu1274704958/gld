#pragma once

#include <node.hpp>
#include <component.h>
#include <mutex>
#include <unordered_map>
#include <ui/word.h>

namespace gld {

	struct WordPatch : Node<Component> {

		struct WordData {
			glm::vec4 uv;
			glm::vec4 uv2;
			glm::vec4 color;
			glm::mat4 model;
			glm::mat4 local;
		};

		WordPatch();
		void create();
		
		void clear();
		void add_word(std::shared_ptr<Word> w, glm::vec2 origin);
		bool has(std::shared_ptr<Texture<TexType::D2>>& tex);
		void create_word_data(std::pair<std::shared_ptr<Word>, glm::vec2>& pair, WordData& res);
		bool refresh(size_t i);
		void refresh();
		size_t word_count();
		std::shared_ptr<Word> get_word(size_t idx);
		void onDraw() override;

	protected:
		size_t submit_word_(std::shared_ptr<Texture<TexType::D2>>& tex, WordData& res);
		void submit_word_(std::shared_ptr<Texture<TexType::D2>>& tex, size_t idx, WordData& res);
		void init_word();
		std::unordered_map<std::shared_ptr<Texture<TexType::D2>>,std::vector<WordData>> word_data;
		std::vector<std::pair<std::shared_ptr<Word>,glm::vec2>> ws;
		//std::unordered_map<size_t, size_t> ws_map;
	};

}