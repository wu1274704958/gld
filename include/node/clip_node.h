#pragma once
#include <node.hpp>

namespace gld {

	template<typename Comp>
	struct ClipNode : public Node<Comp>
	{
		constexpr static unsigned char DefaultStencil = 0x1;
		inline static uint64_t StencilWriteDeep = 0;
		inline static unsigned char NestingMaxStencil = 0x0;
		static void enable()
		{
			glEnable(GL_STENCIL_TEST);
			clear_stencil();
		}
		static void into_clip()
		{
			++StencilWriteDeep;
		}
		static void out_clip()
		{
			--StencilWriteDeep;
		}
		static void clear_stencil()
		{
			if(StencilWriteDeep == 0)
			{
				NestingMaxStencil = 0x0;
				glStencilMask(0xff);
				glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
				glStencilFunc(GL_ALWAYS, 0x0, 0xff);
				glClear( GL_STENCIL_BUFFER_BIT);
				glStencilMask(0x0);
			}
		}

		static void register_nest_stencil(unsigned char val)
		{
			if (val > NestingMaxStencil) NestingMaxStencil = val;
		}

		void onDraw() override 
		{
			if(!debug_clip)glColorMask(0x0, 0x0, 0x0, 0x0);
			glStencilMask(0xff);
			glDepthMask(0x0);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilFunc(GL_GREATER, stencil_val, 0xff);	
			into_clip();	
			register_nest_stencil(stencil_val);
		}
		void onDrawChildren() override 
		{
			glStencilFunc(GL_EQUAL, NestingMaxStencil, 0xff);
			glStencilMask(0x0);
			glDepthMask(0xff);
			glColorMask(0xff, 0xff, 0xff, 0xff);
		}
		void onAfterDraw() override 
		{
			out_clip();
			clear_stencil();
		}

		unsigned char stencil_val = DefaultStencil;
		bool debug_clip = false;
	};
}