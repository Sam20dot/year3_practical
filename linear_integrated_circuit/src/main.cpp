#include<iostream>
#include "../includes/unit2/op_amp_basic.h"


int main () {

    op_amp_basics();
    practical_op_amp();

    // differential amplifier 
    float differential_amp=differential_op_amp(12,34,100);
    std::cout<<"vout is :"<<differential_amp<<"\n";


}
