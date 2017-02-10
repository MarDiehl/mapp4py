#include <stdlib.h>
#include "MAPP.h"
#include "min_cg_dmd.h"
#include "ff_dmd.h"
#include "thermo_dynamics.h"
#include "dynamic_dmd.h"
#include "atoms_styles.h"
using namespace MAPP_NS;

LineSearch* MinCGDMD::ls=NULL;
/*--------------------------------------------
 constructor
 --------------------------------------------*/
MinCGDMD::MinCGDMD(AtomsDMD*& __atoms,ForceFieldDMD*& __ff):
atoms(__atoms),
ff(__ff),
world(__atoms->comm.world),
H_dof{[0 ... __dim__-1][0 ... __dim__-1]=false},
chng_box(false),
e_tol(sqrt(std::numeric_limits<type0>::epsilon())),
affine(false),
max_dx(1.0),
max_dalpha(0.1),
ntally(1000)
{
}
/*--------------------------------------------
 destructor
 --------------------------------------------*/
MinCGDMD::~MinCGDMD()
{
}
/*--------------------------------------------
 error messages
 --------------------------------------------*/
void MinCGDMD::print_error()
{
    if(err==LS_F_DOWNHILL)
        fprintf(MAPP::mapp_out,"line search failed: not downhill direction\n");
    else if(err==LS_F_GRAD0)
        fprintf(MAPP::mapp_out,"line search failed: gradient is zero\n");
    else if(err==LS_MIN_ALPHA)
        fprintf(MAPP::mapp_out,"line search failed: minimum alpha reached\n");
    else if(err==MIN_S_TOLERANCE)
        fprintf(MAPP::mapp_out,"minimization finished: energy tolerance reached\n");
    else if(err==MIN_F_MAX_ITER)
        fprintf(MAPP::mapp_out,"minimization finished: maximum iteration reached\n");
    else if(err==B_F_DOWNHILL)
        fprintf(MAPP::mapp_out,"bracketing failed: not downhill direction\n");
    else if(err==B_F_MAX_ALPHA)
        fprintf(MAPP::mapp_out,"bracketing failed: maximum alpha reached\n");
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::force_calc()
{
    ff->reset();
    if(chng_box)
    {
        ff->derivative_timer(f.A);
        Algebra::DoLT<__dim__>::func([this](int i,int j)
        {
            f.A[i][j]*=H_dof[i][j];
        });
    }
    else
        ff->derivative_timer();
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::prepare_affine_h()
{
    const int natms=atoms->natms;
    if(chng_box)
    {
        Algebra::MLT_mul_MLT(atoms->B,f.A,MLT);
        type0* xvec=x0.vecs[0]->begin();
        type0* hvec=h.vecs[0]->begin();
        for(int iatm=0;iatm<natms;iatm++)
        {
            Algebra::V_mul_MLT(xvec,MLT,hvec);
            xvec+=__dim__;
            hvec+=__dim__;
        }
    }
    else
    {
        type0* hvec=h.vecs[0]->begin();
        for(int iatm=0;iatm<natms;iatm++,hvec+=__dim__)
            Algebra::zero<__dim__>(hvec);
    }
}
/*--------------------------------------------
 
 --------------------------------------------*/
type0 MinCGDMD::calc_ndofs()
{
    return 0.0;
}
/*--------------------------------------------
 init before a run
 --------------------------------------------*/
void MinCGDMD::init()
{
    const int c_dim=atoms->c->dim;
    x.~VecTens();
    new (&x) VecTens<type0,2>(atoms,chng_box,atoms->H,atoms->x,atoms->alpha);
    f.~VecTens();
    new (&f) VecTens<type0,2>(atoms,chng_box,ff->f,ff->f_alpha);
    h.~VecTens();
    new (&h) VecTens<type0,2>(atoms,chng_box,__dim__,c_dim);
    x0.~VecTens();
    new (&x0) VecTens<type0,2>(atoms,chng_box,__dim__,c_dim);
    f0.~VecTens();
    new (&f0) VecTens<type0,2>(atoms,chng_box,__dim__,c_dim);
    
    if(!ls) ls=new LineSearchBrent();
}
/*--------------------------------------------
 finishing minimization
 --------------------------------------------*/
void MinCGDMD::fin()
{
    f0.~VecTens();
    x0.~VecTens();
    h.~VecTens();
    f.~VecTens();
    x.~VecTens();
}
/*--------------------------------------------
 min
 --------------------------------------------*/
void MinCGDMD::run(int nsteps)
{

    init();
    uvecs[0]=atoms->x;
    uvecs[1]=atoms->alpha;
    dynamic=new DynamicDMD(atoms,ff,chng_box,{atoms->elem,atoms->c},
    {h.vecs[0],h.vecs[1],x0.vecs[0],x0.vecs[1],f0.vecs[0],f0.vecs[1]},{});

    
    dynamic->init();
    force_calc();
    type0 S[__dim__][__dim__];
    
    ThermoDynamics thermo(6,
    "PE",ff->nrgy_strss[0],
    "S[0][0]",S[0][0],
    "S[1][1]",S[1][1],
    "S[2][2]",S[2][2],
    "S[1][2]",S[2][1],
    "S[2][0]",S[2][0],
    "S[0][1]",S[1][0]);
    
    Algebra::DoLT<__dim__>::func([this,&S](const int i,const int j)
    {
        S[i][j]=ff->nrgy_strss[1+i+j*__dim__-j*(j+1)/2];
    });
    thermo.init();
    thermo.print(0);
    
    
    curr_energy=ff->nrgy_strss[0];
    type0 prev_energy;
    
    type0 f0_f0,f_f,f_f0;
    type0 ratio,alpha;
    err=LS_S;
    h=f;
    f0_f0=f*f;
    int istep=0;
    for(;istep<nsteps && err==LS_S;istep++)
    {
        if(f0_f0==0.0)
        {
            err=LS_F_GRAD0;
            continue;
        }
        
        x0=x;
        f0=f;
        
        prev_energy=curr_energy;
        
        
        f_h=f*h;
        if(f_h<0.0)
        {
            h=f;
            f_h=f0_f0;
        }
        if(affine) prepare_affine_h();
        err=ls->line_min(this,curr_energy,alpha,1);
        
        if(err!=LS_S)
            continue;
        
        force_calc();
        
        if(prev_energy-curr_energy<e_tol)
            err=MIN_S_TOLERANCE;
        
        if(istep+1==nsteps)
            err=MIN_F_MAX_ITER;
        
        if((istep+1)%ntally==0)
        {
            Algebra::DoLT<__dim__>::func([this,&S](const int i,const int j)
            {S[i][j]=ff->nrgy_strss[1+i+j*__dim__-j*(j+1)/2];});
            thermo.print(istep+1);
        }
        
        if(err) continue;
        
        f_f=f*f;
        f_f0=f*f0;
        
        ratio=(f_f-f_f0)/(f0_f0);
        
        h=ratio*h+f;
        f0_f0=f_f;
    }
    
    if(istep%ntally)
    {
        Algebra::DoLT<__dim__>::func([this,&S](const int i,const int j)
        {S[i][j]=ff->nrgy_strss[1+i+j*__dim__-j*(j+1)/2];});
        thermo.print(istep);
    }

    thermo.fin();
    print_error();
    
    dynamic->fin();
    delete dynamic;
    dynamic=NULL;
    fin();
    
    /*
    type0* xx=atoms->x->begin();
    type0* aa=atoms->alpha->begin();
    type0* cc=atoms->c->begin();
    int n=atoms->natms;
    for(int i=0;i<n;i++)
        printf("%lf %lf %lf %e %e\n",xx[3*i],xx[3*i+1],xx[3*i+2],aa[i],cc[i]);
    */
}
/*--------------------------------------------
 
 --------------------------------------------*/
type0 MinCGDMD::F(type0 alpha)
{
    x=x0+alpha*h;
    type0 max_alpha_lcl=0.0;
    const int n=atoms->natms*atoms->alpha->dim;
    type0* alpha_vec=atoms->alpha->begin();
    type0* c_vec=atoms->c->begin();
    for(int i=0;i<n;i++)
        if(c_vec[i]>0.0) max_alpha_lcl=MAX(max_alpha_lcl,alpha_vec[i]);
    MPI_Allreduce(&max_alpha_lcl,&atoms->max_alpha,1,Vec<type0>::MPI_T,MPI_MAX,world);
    
    if(chng_box)
        atoms->update_H();
    
    dynamic->update(uvecs,2);
    return ff->value_timer();
}
/*--------------------------------------------
 inner product of f and h
 --------------------------------------------*/
type0 MinCGDMD::dF(type0 alpha,type0& drev)
{
    x=x0+alpha*h;
    type0 max_alpha_lcl=0.0;
    const int n=atoms->natms*atoms->alpha->dim;
    type0* alpha_vec=atoms->alpha->begin();
    type0* c_vec=atoms->c->begin();
    for(int i=0;i<n;i++)
        if(c_vec[i]>0.0) max_alpha_lcl=MAX(max_alpha_lcl,alpha_vec[i]);
    MPI_Allreduce(&max_alpha_lcl,&atoms->max_alpha,1,Vec<type0>::MPI_T,MPI_MAX,world);
    
    if(chng_box)
        atoms->update_H();
    
    dynamic->update(uvecs,2);
    force_calc();
    
    drev=-(f*h);
    return ff->nrgy_strss[0];
}
/*--------------------------------------------
 find maximum h
 --------------------------------------------*/
void MinCGDMD::ls_prep(type0& dfa,type0& h_norm,type0& max_a)
{
    
    h_norm=h*h;
    
    dfa=-f_h;
    
    if(h_norm==0.0)
    {
        max_a=0.0;
        dfa=0.0;
        return;
    }
    
    if(dfa>=0.0)
    {
        max_a=0.0;
        dfa=1.0;
        return;
    }
    
    h_norm=sqrt(h_norm);
    
    //type0 max_a_lcl=max_dx;
    type0 max_h_lcl=0.0;
    type0* hvec=h.vecs[0]->begin();
    const int natms=atoms->natms;
    int n=natms*__dim__;
    for(int i=0;i<n;i++)
        max_h_lcl=MAX(max_h_lcl,fabs(hvec[i]));

    
    type0 max_alpha_ratio=std::numeric_limits<type0>::infinity();
    type0* h_alphavec=h.vecs[1]->begin();
    type0* alphavec=x.vecs[1]->begin();
    n=natms*atoms->alpha->dim;
    type0 max_h_alpha_lcl=0.0;
    for(int i=0;i<n;i++)
    {
        if(h_alphavec[i]<0.0)
            max_alpha_ratio=MIN(-alphavec[i]/h_alphavec[i],max_alpha_ratio);
        max_h_alpha_lcl=MAX(max_h_alpha_lcl,fabs(h_alphavec[i]));
    }
    type0 max_a_lcl=MIN(fabs(max_dx/max_h_lcl),max_alpha_ratio);
    max_a_lcl=MIN(max_a_lcl,fabs(max_dalpha/max_h_alpha_lcl));

    MPI_Allreduce(&max_a_lcl,&max_a,1,Vec<type0>::MPI_T,MPI_MIN,world);
}
/*--------------------------------------------
 reset to initial position
 --------------------------------------------*/
void MinCGDMD::F_reset()
{
    x=x0;
    if(chng_box) atoms->update_H();
    dynamic->update(atoms->x);
}
/*------------------------------------------------------------------------------------------------------------------------------------
 
 ------------------------------------------------------------------------------------------------------------------------------------*/
PyObject* MinCGDMD::__new__(PyTypeObject* type,PyObject* args,PyObject* kwds)
{
    Object* __self=reinterpret_cast<Object*>(type->tp_alloc(type,0));
    PyObject* self=reinterpret_cast<PyObject*>(__self);
    return self;
}
/*--------------------------------------------
 
 --------------------------------------------*/
int MinCGDMD::__init__(PyObject* self,PyObject* args,PyObject* kwds)
{
    FuncAPI<OP<AtomsDMD>> f("__init__",{"atoms"});
    
    if(f(args,kwds)==-1) return -1;
    Object* __self=reinterpret_cast<Object*>(self);
    AtomsDMD::Object* atoms=reinterpret_cast<AtomsDMD::Object*>(f.val<0>().ob);
    __self->min=new MinCGDMD(atoms->atoms,atoms->ff);
    __self->atoms=atoms;
    Py_INCREF(atoms);
    return 0;
}
/*--------------------------------------------
 
 --------------------------------------------*/
PyObject* MinCGDMD::__alloc__(PyTypeObject* type,Py_ssize_t)
{
    Object* __self=new Object;
    __self->ob_type=type;
    __self->ob_refcnt=1;
    __self->min=NULL;
    __self->atoms=NULL;
    return reinterpret_cast<PyObject*>(__self);
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::__dealloc__(PyObject* self)
{
    Object* __self=reinterpret_cast<Object*>(self);
    delete __self->min;
    __self->min=NULL;
    if(__self->atoms) Py_DECREF(__self->atoms);
    __self->atoms=NULL;
    delete __self;
}
/*--------------------------------------------*/
PyMethodDef MinCGDMD::methods[]={[0 ... 1]={NULL}};
/*--------------------------------------------*/
void MinCGDMD::setup_tp_methods()
{
    ml_run(methods[0]);
}
/*--------------------------------------------*/
PyTypeObject MinCGDMD::TypeObject={PyObject_HEAD_INIT(NULL)};
/*--------------------------------------------*/
void MinCGDMD::setup_tp()
{
    TypeObject.tp_name="min_cg_dmd";
    TypeObject.tp_doc="conjugate gradient minimization";
    
    TypeObject.tp_flags=Py_TPFLAGS_DEFAULT;
    TypeObject.tp_basicsize=sizeof(Object);
    
    TypeObject.tp_new=__new__;
    TypeObject.tp_init=__init__;
    TypeObject.tp_alloc=__alloc__;
    TypeObject.tp_dealloc=__dealloc__;
    setup_tp_methods();
    TypeObject.tp_methods=methods;
    setup_tp_getset();
    TypeObject.tp_getset=getset;
}
/*--------------------------------------------*/
PyGetSetDef MinCGDMD::getset[]={[0 ... 6]={NULL,NULL,NULL,NULL,NULL}};
/*--------------------------------------------*/
void MinCGDMD::setup_tp_getset()
{
    getset_max_dx(getset[0]);
    getset_e_tol(getset[1]);
    getset_affine(getset[2]);
    getset_H_dof(getset[3]);
    getset_ntally(getset[4]);
    getset_max_dalpha(getset[5]);
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_affine(PyGetSetDef& getset)
{
    getset.name=(char*)"affine";
    getset.doc=(char*)"set to true if transformation is affine";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<bool>::build(reinterpret_cast<Object*>(self)->min->affine,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<bool> affine("affine");
        int ichk=affine.set(op);
        if(ichk==-1) return -1;
        reinterpret_cast<Object*>(self)->min->affine=affine.val;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_H_dof(PyGetSetDef& getset)
{
    getset.name=(char*)"H_dof";
    getset.doc=(char*)"unitcell degrees of freedom";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<symm<bool[__dim__][__dim__]>>::build(reinterpret_cast<Object*>(self)->min->H_dof,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<symm<bool[__dim__][__dim__]>> H_dof("H_dof");
        int ichk=H_dof.set(op);
        if(ichk==-1) return -1;
        
        bool (&__H_dof)[__dim__][__dim__]=reinterpret_cast<Object*>(self)->min->H_dof;
        bool chng_box=false;
        Algebra::DoLT<__dim__>::func([&H_dof,&__H_dof,&chng_box](int i,int j)
        {
            __H_dof[i][j]=__H_dof[j][i]=H_dof.val[i][j];
            if(__H_dof[i][j]) chng_box=true;
        });
        reinterpret_cast<Object*>(self)->min->chng_box=chng_box;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_e_tol(PyGetSetDef& getset)
{
    getset.name=(char*)"e_tol";
    getset.doc=(char*)"energy tolerance criterion for stopping minimization";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<type0>::build(reinterpret_cast<Object*>(self)->min->e_tol,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<type0> e_tol("e_tol");
        e_tol.logics[0]=VLogics("ge",0.0);
        int ichk=e_tol.set(op);
        if(ichk==-1) return -1;
        reinterpret_cast<Object*>(self)->min->e_tol=e_tol.val;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_max_dx(PyGetSetDef& getset)
{
    getset.name=(char*)"max_dx";
    getset.doc=(char*)"maximum displacement";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<type0>::build(reinterpret_cast<Object*>(self)->min->max_dx,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<type0> max_dx("max_dx");
        max_dx.logics[0]=VLogics("gt",0.0);
        int ichk=max_dx.set(op);
        if(ichk==-1) return -1;
        reinterpret_cast<Object*>(self)->min->max_dx=max_dx.val;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_max_dalpha(PyGetSetDef& getset)
{
    getset.name=(char*)"max_dalpha";
    getset.doc=(char*)"maximum alpha displacement";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<type0>::build(reinterpret_cast<Object*>(self)->min->max_dx,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<type0> max_dalpha("max_dalpha");
        max_dalpha.logics[0]=VLogics("gt",0.0);
        int ichk=max_dalpha.set(op);
        if(ichk==-1) return -1;
        reinterpret_cast<Object*>(self)->min->max_dalpha=max_dalpha.val;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::getset_ntally(PyGetSetDef& getset)
{
    getset.name=(char*)"ntally";
    getset.doc=(char*)"tally thermodynamic quantities every ntally steps";
    getset.get=[](PyObject* self,void*)->PyObject*
    {
        return var<int>::build(reinterpret_cast<Object*>(self)->min->ntally,NULL);
    };
    getset.set=[](PyObject* self,PyObject* op,void*)->int
    {
        VarAPI<int> ntally("ntally");
        ntally.logics[0]=VLogics("gt",0);
        int ichk=ntally.set(op);
        if(ichk==-1) return -1;
        reinterpret_cast<Object*>(self)->min->ntally=ntally.val;
        return 0;
    };
}
/*--------------------------------------------
 
 --------------------------------------------*/
void MinCGDMD::ml_run(PyMethodDef& tp_methods)
{
    tp_methods.ml_flags=METH_VARARGS | METH_KEYWORDS;
    tp_methods.ml_name="run";
    tp_methods.ml_doc="run energy minimization";
    
    tp_methods.ml_meth=(PyCFunction)(PyCFunctionWithKeywords)
    [](PyObject* self,PyObject* args,PyObject* kwds)->PyObject*
    {
        Object* __self=reinterpret_cast<Object*>(self);
        FuncAPI<int> f("run",{"max_nsteps"});
        f.logics<0>()[0]=VLogics("ge",0);
        if(f(args,kwds)) return NULL;
        __self->min->run(f.val<0>());
        Py_RETURN_NONE;
    };
}