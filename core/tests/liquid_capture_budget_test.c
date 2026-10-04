#include <stdio.h>
#include "core/liquid/liquid_capture_budget.h"
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    int demand[]={320,320,320,320}, admitted[4];
    CHECK(LiquidCaptureBudget_Allocate(demand,4,384,admitted)==384);
    for(int i=0;i<4;i++) CHECK(admitted[i]==96);
    CHECK(fabsf(LiquidCaptureBudget_OpticalScale(320,96)-sqrtf(320.0f/96.0f))<1e-6f);
    CHECK(LiquidCaptureBudget_OpticalScale(320,320)==1.0f);
    CHECK(LiquidCaptureBudget_OpticalScale(320,1)==2.0f);
    CHECK(LiquidCaptureBudget_OpticalScale(320,0)==1.0f);
    demand[0]=12;
    CHECK(LiquidCaptureBudget_Allocate(demand,4,384,admitted)==384);
    CHECK(admitted[0]==12);
    for(int i=1;i<4;i++) CHECK(admitted[i]==124);
    demand[1]=demand[2]=demand[3]=0;
    CHECK(LiquidCaptureBudget_Allocate(demand,4,384,admitted)==12);
    CHECK(LiquidCaptureBudget_Allocate(demand,4,0,admitted)==0);
    CHECK(LiquidCaptureBudget_Allocate(demand,0,384,admitted)==0);
    puts("PASS: liquid_capture_budget_test");
    return 0;
}
