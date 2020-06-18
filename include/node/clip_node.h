#pragma once
#include <node.hpp>

namespace gld {

	template<typename Comp>
	struct ClipNode : public Node<Comp>
	{
		constexpr static unsigned char DefaultStencil = 0x1;

		static void enable()
		{
			glEnable(GL_STENCIL_TEST);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0x0);
		}

		void onDraw() override 
		{
			glStencilMask(0xff);
			glStencilFunc(GL_ALWAYS, stencil_val, 0xff);
		}
		void onDrawChildren() override 
		{
			glStencilFunc(GL_EQUAL, stencil_val, 0xff);
			glStencilMask(0x0);
		}
		void onAfterDraw() override 
		{
			glStencilMask(0xff);
			glClear( GL_STENCIL_BUFFER_BIT);
			glStencilMask(0x0);
		}

		unsigned char stencil_val = DefaultStencil;
	};
}