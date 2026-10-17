#include "../src/transport.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint32_t rate=22050, end=2016769;
    transport_t t={0,123};
    transport_seek(&t,end,rate,-5); assert(t.position==0 && t.phase==0);
    transport_seek(&t,end,rate,5); assert(t.position==110250);
    transport_seek(&t,end,rate,5); assert(t.position==220500);
    transport_seek(&t,end,rate,-5); assert(t.position==110250);
    t.position=end-10; t.phase=123;
    transport_seek(&t,end,rate,5); assert(t.position==end && !t.phase);
    transport_seek(&t,end,rate,-5); assert(t.position==end-110250);
    transport_seek(&t,end,rate,-1000); assert(t.position==0);
    transport_seek(&t,end,rate,1000); assert(t.position==end);
    puts("Seeking, fractional phase reset, track start/end boundaries passed");
}
