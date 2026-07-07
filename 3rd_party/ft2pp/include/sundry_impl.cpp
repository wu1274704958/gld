#pragma once 

namespace png_sundry
{
	dyn_op::dyn_op(char* str)
	{
		if (std::strcmp("==", str) == 0) { op = OP::Eq; }
		else
		if (std::strcmp("!=", str) == 0) { op = OP::NotEq; }
		else
		if (std::strcmp("<", str) == 0) { op = OP::Less; }
		else
		if (std::strcmp(">", str) == 0) { op = OP::Large; }
		else
		if (std::strcmp("<=", str) == 0) { op = OP::LessEq; }
		else
		if (std::strcmp(">=", str) == 0) { op = OP::LargeEq; }
	}
}