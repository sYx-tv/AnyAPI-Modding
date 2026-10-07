#pragma once
#include <cmath>
namespace wheel {
constexpr double pi=3.14159265358979323846;
// First sector is centred at twelve o'clock; centre and outside cancel.
inline int sector(double x,double y,double deadzone,double radius,int count){
 if(count<=0)return -1;double d=std::hypot(x,y);if(d<deadzone||d>radius)return -1;
 double a=std::atan2(y,x)+pi/2+pi/count;a=std::fmod(a+2*pi,2*pi);return int(a/(2*pi/count))%count;
}
}
