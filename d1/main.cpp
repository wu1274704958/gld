#include <RenderDemo.h>
#include <cstdio>

class Demo1 : public RenderDemo{
public:
    int init() override
    {
        return 0;
    }

    void draw() override
    {

    }
};


int main()
{
    Demo1 d;
    if( d.initWindow(300,300,"Demo1"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}