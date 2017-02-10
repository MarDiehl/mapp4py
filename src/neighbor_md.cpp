/*--------------------------------------------
 Created by Sina on 06/05/13.
 Copyright (c) 2013 MIT. All rights reserved.
 --------------------------------------------*/
#include "neighbor_md.h"
#include "ff_md.h"
#include "atoms_md.h"
#include "memory.h"
#include "MAPP.h"
#include "xmath.h"

using namespace MAPP_NS;
/*--------------------------------------------
 constructor
 --------------------------------------------*/
NeighborMD::NeighborMD(AtomsMD* __atoms,
type0**& __cut_sk_sq):
elem(__atoms->elem),
cut_sk_sq(__cut_sk_sq),
Neighbor(__atoms)
{
}
/*--------------------------------------------
 destructor
 --------------------------------------------*/
NeighborMD::~NeighborMD()
{
}
/*--------------------------------------------
 initiation before MD
 --------------------------------------------*/
void NeighborMD::init()
{
    Neighbor::init();
}
/*--------------------------------------------
 finalize MD
 --------------------------------------------*/
void NeighborMD::fin()
{
    if(neighbor_list_size_size)
    {
        for(int i=0;i<neighbor_list_size_size;i++)
            delete [] neighbor_list[i];
        delete [] neighbor_list;
        delete [] neighbor_list_size;
    }
    neighbor_list_size_size=0;
    neighbor_list=NULL;
    neighbor_list_size=NULL;

    Neighbor::fin();
}
/*--------------------------------------------
 create the neighbopr list
 s_or_x: 0 for x, 1 for s
 --------------------------------------------*/
void NeighborMD::create_list(bool box_change)
{
    
    for(int i=0;i<neighbor_list_size_size;i++)
        delete [] neighbor_list[i];
    delete [] neighbor_list;
    delete [] neighbor_list_size;
    

    neighbor_list_size_size=atoms->natms;
    
    Memory::alloc(neighbor_list,neighbor_list_size_size);
    Memory::alloc(neighbor_list_size,neighbor_list_size_size);
    for(int i=0;i<neighbor_list_size_size;i++)
        neighbor_list_size[i]=0;

    
    elem_type* elem_vec=elem->begin();
    elem_type elem_i;
    
    
    
    int* tmp_neigh_list=NULL;
    Memory::alloc(tmp_neigh_list,neighbor_list_size_size+atoms->natms_ph);
    int tmp_neigh_list_sz=0;
    const type0* x=atoms->x->begin();
    no_pairs=0;
    
    type0 x_i[__dim__];
    auto store_xi=[&x_i,&x,&elem_i,&elem_vec](int i)->void
    {
        Algebra::V_eq<__dim__>(x+i*__dim__,x_i);
        elem_i=elem_vec[i];
    };
    
    auto store=[this,&tmp_neigh_list_sz,&tmp_neigh_list](int i)->void
    {
        neighbor_list[i]=NULL;
        if(tmp_neigh_list_sz)
        {
            neighbor_list[i]=new int[tmp_neigh_list_sz];
            neighbor_list_size[i]=tmp_neigh_list_sz;
            memcpy(neighbor_list[i],tmp_neigh_list,tmp_neigh_list_sz*sizeof(int));
            no_pairs+=tmp_neigh_list_sz;
        }
        tmp_neigh_list_sz=0;
    };
    
    if(pair_wise)
    {
        cell->DO(box_change,store_xi,
        [&tmp_neigh_list,&tmp_neigh_list_sz,&elem_vec,&x,&x_i,&elem_i,this](int i,int j)
        {
            if(i<j && Algebra::RSQ<__dim__>(x_i,x+j*__dim__)<cut_sk_sq[elem_i][elem_vec[j]])
                tmp_neigh_list[tmp_neigh_list_sz++]=j;
        },store);
    }
    else
    {
        cell->DO(box_change,store_xi,
        [&tmp_neigh_list,&tmp_neigh_list_sz,&elem_vec,&x,&x_i,&elem_i,this](int i,int j)
        {
            if(i!=j && Algebra::RSQ<__dim__>(x_i,x+j*__dim__)<cut_sk_sq[elem_i][elem_vec[j]])
                tmp_neigh_list[tmp_neigh_list_sz++]=j;
        },store);
        no_pairs/=2;
    }
    
    Memory::dealloc(tmp_neigh_list);
    no_neigh_lists++;

}