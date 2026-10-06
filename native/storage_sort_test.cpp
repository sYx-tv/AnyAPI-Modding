#include "storage_sort.h"
#include <cassert>
#include <iostream>
static AnyStoredItemV1 item(uint64_t id,const char* name,int qty,int x,int y,int w=1,int h=1){AnyStoredItemV1 a;a.token=id;a.grid=100;a.side=1;a.quantity=qty;a.width=w;a.height=h;a.x=x;a.y=y;strcpy_s(a.name,name);return a;}
int main(){AnyStorageGridV1 g;g.token=100;g.side=1;g.width=2;g.height=1;g.allow_rotation=1;auto p=g;p.token=200;p.side=0;std::vector<AnyStorageGridV1> grids{g,p};std::vector<AnyStoredItemV1> items{item(1,"Zinc",8,0,0),item(2,"Apple",3,1,0)};std::vector<storage_sort::Placement> moves;std::string error;auto accepts=[](uint64_t,uint64_t,int){return true;};
 assert(storage_sort::plan(items,grids,false,true,accepts,moves,error)&&moves.size()==3);assert(moves[0].grid==200);for(auto& m:moves){auto a=std::find_if(items.begin(),items.end(),[&](auto& i){return i.token==m.item;});for(auto& b:items)if(a->token!=b.token)assert(!storage_sort::overlap(m,*a,b));a->grid=m.grid;a->x=m.x;a->y=m.y;a->rotation=m.rotation;}assert(items[0].x==1&&items[1].x==0&&items[0].grid==100&&items[1].grid==100);
 assert(storage_sort::plan(items,grids,true,true,accepts,moves,error)&&moves.size()==3);
 assert(!storage_sort::plan(items,{g},true,true,accepts,moves,error)&&moves.empty());
 p.width=1;p.height=1;auto occupied=item(3,"Backpack contents",1,0,0);occupied.grid=200;occupied.side=0;items.push_back(occupied);assert(!storage_sort::plan(items,{g,p},true,true,accepts,moves,error)&&moves.empty());
 items.pop_back();g.width=3;g.height=2;items={item(1,"Large",1,0,0,2,1),item(2,"Small",4,2,1)};assert(storage_sort::plan(items,{g,p},false,true,accepts,moves,error));
 auto blocked=[](uint64_t id,uint64_t grid,int){return id!=1||grid!=100;};assert(!storage_sort::plan(items,{g,p},false,true,blocked,moves,error)&&moves.empty());items[0].width=0;assert(!storage_sort::plan(items,{g,p},false,true,accepts,moves,error)&&moves.empty());items={item(1,"Apple",1,0,0)};g.width=4;g.height=5;assert(storage_sort::plan(items,{g},false,true,accepts,moves,error)&&moves.size()==1&&moves[0].x==0&&moves[0].y==4);std::cout<<"PASS: top-first alphabetical/quantity packing, full-grid cycles, collision-free plan and refusal checks\n";
}
