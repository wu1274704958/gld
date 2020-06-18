#include <data_mgr.hpp>
#include <sundry.hpp>
#include <generator/Generator.hpp>

using namespace gld;

std::string GenSquareIndices::key_from_args(GenSquareIndices::ArgsTy args)
{
	return "GenSquareIndices[0,1,2,0,2,3]";
}

GenSquareIndices::RealRetTy GenSquareIndices::load(GenSquareIndices::ArgsTy args)
{
	return std::make_tuple(true, std::shared_ptr<std::vector<int>>(new std::vector<int>({ 0,1,2,0,2,3 })));
}

GenSquareIndices::ArgsTy GenSquareIndices::default_args()
{
	return std::tuple<>();
}


std::string GenSquareVertices::key_from_args(GenSquareVertices::ArgsTy args)
{
	return sundry::format_tup(args, '#');
}

GenSquareVertices::RealRetTy GenSquareVertices::load(GenSquareVertices::ArgsTy args)
{
	auto [x, y] = args;
	return std::make_tuple(true, std::shared_ptr<std::vector<float>>(new std::vector<float>(gen::quad(glm::vec2(x, y)))));
}
GenSquareVertices::ArgsTy GenSquareVertices::default_args()
{
	return std::make_tuple(0.5f, 0.5f);
}