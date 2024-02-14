#include<bits/stdc++.h>
using namespace std;
long double deg,sine;
long double getnextsine(long double cursine){
	long double mem=1-sqrt(1-cursine*cursine);
	return sqrt(cursine*cursine+mem*mem)/2;
}
int main(){
	cin>>deg;
	for(long double slicedeg=90,slicesine=1;slicesine;slicedeg/=2,slicesine=getnextsine(slicesine))
		if(slicedeg<=deg)
			sine=sine*sqrt(1-slicesine*slicesine)+sqrt(1-sine*sine)*slicesine,deg-=slicedeg;
	cout<<fixed<<setprecision(18)<<sine;
	return 0;
}
