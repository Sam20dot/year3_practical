#include<iostream> 
#include "../../includes/unit2/op_amp_basic.h"

void op_amp_basics() {

    std::cout<<"______ OP_AMPS__BASICS ___\n";
    
    std::cout<<"op amps has third min parts "
             <<"1. input signal\n 2. output signal\n 3.voltage supply\n";
    return;


}

void practical_op_amp() {
    std::cout<<"____PRACTICAL OP_AMP_____\n"
             <<"1.input signal\n 2. ouput signal \n 3.voltage supply\n load resistances";




}

void ac_op_amp() {

    std::cout<<"the op amp to ouput the complete the ac we have to revise the \n"
        "the plarity so that it can the signal can be changed back and fourth to produce that ac\n";


}
float differential_op_amp(float vin1,float vin2,float Ac) {

    float vout=Ac*(vin1-vin2);

    return vout;


}


