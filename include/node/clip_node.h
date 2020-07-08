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
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glStencilMask(0x0);
		}

		void onDraw() override 
		{
			glColorMask(0x0, 0x0, 0x0, 0x0);
			glStencilMask(0xff);
			glDepthMask(0x0);
			glStencilOp(GL_ZERO, GL_KEEP, GL_REPLACE);
			glStencilFunc(GL_ALWAYS, stencil_val, 0xff);		
		}
		void onDrawChildren() override 
		{
			glStencilFunc(GL_EQUAL, stencil_val, 0xff);
			glStencilMask(0x0);
			glDepthMask(0xff);
			glColorMask(0xff, 0xff, 0xff, 0xff);
		}
		void onAfterDraw() override 
		{
			glStencilMask(0xff);
			glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
			glStencilFunc(GL_ALWAYS, stencil_val, 0xff);
			glClear( GL_STENCIL_BUFFER_BIT);
			glStencilMask(0x0);
		}

		unsigned char stencil_val = DefaultStencil;
	};
}