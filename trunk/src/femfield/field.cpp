/*
DelFEM (Finite Element Analysis)
Copyright (C) 2009  Nobuyuki Umetani    n.umetani@gmail.com

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

////////////////////////////////////////////////////////////////
// Field.cpp：場クラス(CField)の実装
////////////////////////////////////////////////////////////////


#if defined(__VISUALC__)
#pragma warning ( disable : 4786 )
#pragma warning ( disable : 4996 )
#endif

#define for if(0);else for

#include <math.h>
#include <fstream>
#include <stdio.h>

#include "delfem/field.h"
#include "delfem/field_world.h"
#include "delfem/eval.h"
#include "delfem/femeqn/ker_emat_hex.h"
#include "delfem/femeqn/ker_emat_tet.h"

using namespace Fem::Field;
using namespace MatVec;

static double TriArea(const double p0[], const double p1[], const double p2[]){
	return 0.5*( (p1[0]-p0[0])*(p2[1]-p0[1])-(p2[0]-p0[0])*(p1[1]-p0[1]) );
}

static void TriDlDx(double dldx[][2], double const_term[],
			 const double p0[], const double p1[], const double p2[]){

	const double area = TriArea(p0,p1,p2);
	const double tmp1 = 0.5 / area;

	const_term[0] = tmp1*(p1[0]*p2[1]-p2[0]*p1[1]);
	const_term[1] = tmp1*(p2[0]*p0[1]-p0[0]*p2[1]);
	const_term[2] = tmp1*(p0[0]*p1[1]-p1[0]*p0[1]);

	dldx[0][0] = tmp1*(p1[1]-p2[1]);
	dldx[1][0] = tmp1*(p2[1]-p0[1]);
	dldx[2][0] = tmp1*(p0[1]-p1[1]);

	dldx[0][1] = tmp1*(p2[0]-p1[0]);
	dldx[1][1] = tmp1*(p0[0]-p2[0]);
	dldx[2][1] = tmp1*(p1[0]-p0[0]);
}


bool CField::CValueFieldDof::GetValue(double cur_t, double& value) const {
    if( itype == 0 ){ return false; }
    if( itype == 1 ){ value = val; return true; }
    if( itype == 2 ){
        Fem::Field::CEval eval;
        eval.SetKey("t",cur_t);
        if( !eval.SetExp(math_exp) ){ return false; }
        value = eval.Calc();
        return true;
    }
    return false;
}

CField::CField(const CField& rhs)
{
	m_is_valid = rhs.m_is_valid;
  m_id_field_parent = rhs.m_id_field_parent;
	m_ndim_coord = rhs.m_ndim_coord;
	m_aElemIntp = rhs.m_aElemIntp;
  
	m_na_c = rhs.m_na_c;
	m_na_e = rhs.m_na_e;
	m_na_b = rhs.m_na_b;
  
	m_field_type = rhs.m_field_type;
	m_field_derivative_type = rhs.m_field_derivative_type;
	m_name = rhs.m_name;  
	m_map_val2co = rhs.m_map_val2co;
  
	////////////////
	m_DofSize = rhs.m_DofSize;
	m_aValueFieldDof = rhs.m_aValueFieldDof;  
	m_id_field_dep = rhs.m_id_field_dep;
	m_is_gradient = rhs.m_is_gradient;
}

CField::CField
(unsigned int id_field_parent,	// 親フィールド
 const std::vector<CElemInterpolation>& aEI, 
 const CNodeSegInNodeAry& nsna_c, const CNodeSegInNodeAry& nsna_b, 
 CFieldWorld& world)
{
	m_aElemIntp = aEI;

	m_na_c = nsna_c;
	m_na_b = nsna_b;

	m_ndim_coord = 0;
	if( m_na_c.id_na_co ){
		const CNodeAry& na = world.GetNA( m_na_c.id_na_co );
    assert( na.IsSegID( m_na_c.id_ns_co ) );
		const CNodeAry::CNodeSeg& ns_co = na.GetSeg( m_na_c.id_ns_co );
		m_ndim_coord = ns_co.Length();
	}
	else if( m_na_b.id_na_co ){
		const CNodeAry& na = world.GetNA( m_na_b.id_na_co );
    assert( na.IsSegID( m_na_b.id_ns_co ) );
		const CNodeAry::CNodeSeg& ns_co = na.GetSeg( m_na_b.id_ns_co );
		m_ndim_coord = ns_co.Length();
	}
  
//  std::cout << "field : " << m_na_b.id_na_co << " " << m_na_b.id_ns_co << std::endl;

	m_field_type = NO_VALUE;
	m_field_derivative_type = 0;
	m_DofSize = 0;

	m_id_field_parent = id_field_parent;
	if( m_id_field_parent == 0 ){	
		assert( m_na_c.is_part_va == false );
	}
	else{
		assert( world.IsIdField(m_id_field_parent) );
		const CField& field_parent = world.GetField(m_id_field_parent);
		this->m_DofSize = field_parent.GetNLenValue();
		this->m_field_type = field_parent.GetFieldType();
		this->m_field_derivative_type = field_parent.GetFieldDerivativeType();
	}

	// map_val2co
	if(    m_na_c.id_na_va != 0 
		&& m_na_c.id_na_va != m_na_c.id_na_co
		&& m_na_c.id_na_co != 0 )
	{
		const CNodeAry& na_va = world.GetNA(m_na_c.id_na_va);
		const unsigned int nnode_va = na_va.Size();
		m_map_val2co.resize(nnode_va);
		for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
			const CElemInterpolation& ei = m_aElemIntp[iei];
			const CElemAry& ea = world.GetEA(ei.id_ea);
			const CElemAry::CElemSeg& es_co = ea.GetSeg(ei.id_es_c_co);
			const CElemAry::CElemSeg& es_val = ea.GetSeg(ei.id_es_c_va);
			unsigned int noes_co[10];
			unsigned int noes_val[10];
			unsigned int nnoes = es_co.Length();
			assert( es_co.Size() == es_val.Size() );
			for(unsigned int ielem=0;ielem<ea.Size();ielem++){
				es_co.GetNodes(ielem,noes_co);
				es_val.GetNodes(ielem,noes_val);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					m_map_val2co[ noes_val[inoes] ] = noes_co[inoes];
				}
			}
		}
	}

	m_id_field_dep = 0;
	m_is_gradient = false;
//	m_is_mises = false;
//	m_is_maxprinciple = false;

	m_is_valid = this->AssertValid(world);
	assert( m_is_valid );
}


bool CField::AssertValid(CFieldWorld& world) const
{
	{	// fieldが持っているeaが正しくnaを参照してるか調べる
		for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
			const Field::CField::CElemInterpolation& ei = m_aElemIntp[iei];
			unsigned int id_ea = ei.id_ea;
			assert( world.IsIdEA(id_ea) );
			const CElemAry& ea = world.GetEA(id_ea);
			if( ei.id_es_c_co != 0 ){
				unsigned int id_es_c_coord = ei.id_es_c_co;
				assert( ea.IsSegID(id_es_c_coord) );
				const CElemAry::CElemSeg& es = ea.GetSeg(id_es_c_coord);
				const unsigned int id_na_co = es.GetIdNA();
				assert( this->m_na_c.id_na_co == id_na_co );
				assert( world.IsIdNA(id_na_co) );
				const CNodeAry& na_co = world.GetNA(id_na_co);
				assert( es.GetMaxNoes() < na_co.Size() );
			}
			if( ei.id_es_c_va != 0 ){
				unsigned int id_es_c_va = ei.id_es_c_va;
				assert( ea.IsSegID(id_es_c_va) );
				const CElemAry::CElemSeg& es_va = ea.GetSeg(id_es_c_va);
				unsigned int id_na_va = es_va.GetIdNA();
				assert( this->m_na_c.id_na_va == id_na_va );
				assert( world.IsIdNA(id_na_va) );
				const CNodeAry& na_va = world.GetNA(id_na_va);
				assert( es_va.GetMaxNoes() < na_va.Size() );
			}
		}
	}
	{	// m_dim_coordを調べる
		if( world.IsIdNA(m_na_c.id_na_co) ){
			assert( world.IsIdNA(m_na_c.id_na_co) );
			const CNodeAry& na_c_coord = world.GetNA(m_na_c.id_na_co);
			assert( na_c_coord.IsSegID(m_na_c.id_ns_co) );
			const CNodeAry::CNodeSeg& ns_c_coord = na_c_coord.GetSeg(m_na_c.id_ns_co);
			assert( this->m_ndim_coord == ns_c_coord.Length() );
		}
		// TODO : bubbleやedgeについても調べたい
	}
	{	// cornerのvalueが部分場かどうか示すフラグが合ってるかどうか調べる
		if( m_na_c.id_na_va != 0 ){
			assert( world.IsIdNA(m_na_c.id_na_va) );
			CNodeAry& na_val = world.GetNA(m_na_c.id_na_va);
			unsigned int nnode_val = na_val.Size();
			std::vector<unsigned int> flag_vec;
			flag_vec.resize( nnode_val, 0 );
			////////////////
			for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
				const unsigned int id_ea = m_aElemIntp[iei].id_ea;
				const CElemAry& ea = world.GetEA(id_ea);
				unsigned int id_es_val = m_aElemIntp[iei].id_es_c_va;
				assert( ea.IsSegID(id_es_val) );
				const CElemAry::CElemSeg& es_val = ea.GetSeg(id_es_val);
				assert( es_val.GetMaxNoes() < nnode_val );
				unsigned int nnoes = es_val.Length();
				unsigned int nno_c[64];
				for(unsigned int ielem=0;ielem<ea.Size();ielem++){
					es_val.GetNodes(ielem,nno_c);
					for(unsigned int inoes=0;inoes<nnoes;inoes++){
						assert( nno_c[inoes] < nnode_val );
						flag_vec[ nno_c[inoes] ] = 1;
					}
				}
			}
			bool iflag1 = false;
			for(unsigned int ino=0;ino<nnode_val;ino++){
				if( flag_vec[ino] == 0 ){ iflag1 = true; break; }
			}
//			assert( m_na_c.is_part_va == iflag1 );
		}
	}
	{	// cornerのcoordが部分場かどうか示すフラグが合ってるかどうか調べる
		if( m_na_c.id_na_co != 0 ){
			assert( world.IsIdNA(m_na_c.id_na_co) );
			CNodeAry& na = world.GetNA(m_na_c.id_na_co);
			unsigned int nnode_co = na.Size();
			std::vector<unsigned int> flag_vec;
			flag_vec.resize( nnode_co, 0 );
			////////////////
			for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
				const unsigned int id_ea = m_aElemIntp[iei].id_ea;
				assert( world.IsIdEA(id_ea) );
				const CElemAry& ea = world.GetEA(id_ea);
				unsigned int id_es_co = m_aElemIntp[iei].id_es_c_co;
				assert( ea.IsSegID(id_es_co) );
				const CElemAry::CElemSeg& es_co = ea.GetSeg(id_es_co);
				assert( es_co.GetMaxNoes() < nnode_co );
				unsigned int nnoes = es_co.Length();
				unsigned int no_c[64];
				for(unsigned int ielem=0;ielem<ea.Size();ielem++){
					es_co.GetNodes(ielem,no_c);
					for(unsigned int inoes=0;inoes<nnoes;inoes++){
						assert( no_c[inoes] < nnode_co );
						flag_vec[ no_c[inoes] ] = 1;
					}
				}
			}
			bool iflag1 = false;
			for(unsigned int ino=0;ino<nnode_co;ino++){
				if( flag_vec[ino] == 0 ){ iflag1 = true; break; }
			}
//			assert( m_na_c.is_part_co == iflag1 );
		}
	}
	return true;
}

const CNodeAry::CNodeSeg& CField::GetNodeSeg
(ELSEG_TYPE elseg_type, 
 bool is_value, const CFieldWorld& world, unsigned int fdt ) const
{
	unsigned int id_na = 0;
	unsigned int id_ns = 0;
	if(      elseg_type == CORNER ){
		if( is_value ){
			id_na = this->m_na_c.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_c.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ac; }
		}
		else{
			id_na = this->m_na_c.id_na_co;
			id_ns = this->m_na_c.id_ns_co;
		}
	}
	else if( elseg_type == BUBBLE ){
		if( is_value ){
			id_na = this->m_na_b.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_b.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ac; }
		}
		else{
			id_na = this->m_na_b.id_na_co;
			id_ns = this->m_na_b.id_ns_co;
		}
	}
//  std::cout << "IsNodeSeg : " << id_na << " " << id_ns << std::endl;
	assert( world.IsIdNA(id_na) );
	const CNodeAry& na = world.GetNA(id_na);
	assert( na.IsSegID(id_ns) );
	return na.GetSeg(id_ns);

}

bool CField::IsNodeSeg(ELSEG_TYPE elseg_type,
					   bool is_value, const CFieldWorld& world, unsigned int fdt) const
{
	unsigned int id_na = 0;
	unsigned int id_ns = 0;
	if(      elseg_type == CORNER ){
		if( is_value ){
			id_na = this->m_na_c.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_c.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ac; }
		}
		else{
			id_na = this->m_na_c.id_na_co;
			id_ns = this->m_na_c.id_ns_co;
		}
	}
	else if( elseg_type == BUBBLE ){
		if( is_value ){
			id_na = this->m_na_b.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_b.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ac; }
		}
		else{
			id_na = this->m_na_b.id_na_co;
			id_ns = this->m_na_b.id_ns_co;
		}
	}
	if( !world.IsIdNA(id_na) ) return false;
	const CNodeAry& na = world.GetNA(id_na);
	if( !na.IsSegID(id_ns) ) return false;
	return true;
}


CNodeAry::CNodeSeg& CField::GetNodeSeg(
	ELSEG_TYPE elseg_type, 
	bool is_value, CFieldWorld& world, unsigned int fdt )
{
	unsigned int id_na = 0;
	unsigned int id_ns = 0;
	if(      elseg_type == CORNER ){
		if( is_value ){
			id_na = this->m_na_c.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_c.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_c.id_ns_ac; }
		}
		else{
			id_na = this->m_na_c.id_na_co;
			id_ns = this->m_na_c.id_ns_co;
		}
	}
	else if( elseg_type == BUBBLE ){
		if( is_value ){
			id_na = this->m_na_b.id_na_va;
			if( fdt & 1               ){ id_ns = this->m_na_b.id_ns_va; }
			if( fdt & 2 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ve; }
			if( fdt & 4 && id_ns == 0 ){ id_ns = this->m_na_b.id_ns_ac; }
		}
		else{
			id_na = this->m_na_b.id_na_co;
			id_ns = this->m_na_b.id_ns_co;
		}
	}
	assert( world.IsIdNA(id_na) );
	CNodeAry& na = world.GetNA(id_na);
	assert( na.IsSegID(id_ns) );
	return na.GetSeg(id_ns);

}

const CElemAry::CElemSeg& CField::GetElemSeg(unsigned int id_ea, ELSEG_TYPE elseg_type, bool is_value, const CFieldWorld& world ) const
{
	unsigned int id_es = this->GetIdElemSeg(id_ea,elseg_type,is_value,world);
	if( id_es == 0 ){// try partiel element
    const CElemAry& ea = world.GetEA(id_ea);
    unsigned int id_na_co = this->GetNodeSegInNodeAry(CORNER).id_na_co;
    unsigned int id_na_va = this->GetNodeSegInNodeAry(CORNER).id_na_va;    
    const std::vector<unsigned int>& aIdES = ea.GetAry_SegID();
    for(unsigned int iies=0;iies<aIdES.size();iies++){
      unsigned int id_es0 = aIdES[iies];
      const CElemAry::CElemSeg& es = ea.GetSeg(id_es0);
      if( is_value ){ if( es.GetIdNA() == id_na_va ){ id_es = id_es0; break; } }
      else{           if( es.GetIdNA() == id_na_co ){ id_es = id_es0; break; } }
    }
    if( id_es == 0 ){
      assert(0);
      throw 0;
    }
	}
	const CElemAry& ea = world.GetEA(id_ea);
	assert( ea.IsSegID(id_es) );
	return ea.GetSeg(id_es);
}

unsigned int CField::GetIdElemSeg(unsigned int id_ea, ELSEG_TYPE elseg_type, bool is_value, const CFieldWorld& world ) const
{
	if( !world.IsIdEA(id_ea) ){ return false; }
	unsigned int iei;
	for(iei=0;iei<m_aElemIntp.size();iei++){
		if( m_aElemIntp[iei].id_ea == id_ea ){ break; }
	}
	if( iei == m_aElemIntp.size() ){ return 0; }
	const CElemInterpolation& ei = m_aElemIntp[iei];
	unsigned int id_es = 0;
	if( is_value ){
		if(      elseg_type == CORNER ){ id_es = ei.id_es_c_va; }
		else if( elseg_type == BUBBLE ){ id_es = ei.id_es_b_va; }
		else if( elseg_type == EDGE   ){ id_es = ei.id_es_e_va; }
		else{ assert(0); }
	}
	else{
		if(      elseg_type == CORNER ){ id_es = ei.id_es_c_co; }
		else if( elseg_type == BUBBLE ){ id_es = ei.id_es_b_co; }
		else if( elseg_type == EDGE   ){ id_es = ei.id_es_e_co; }
		else{ assert(0); }
	}
	const CElemAry& ea = world.GetEA(id_ea);
	if( !ea.IsSegID(id_es) ) return 0;
	return id_es;
}


int CField::GetLayer(unsigned int id_ea) const
{
	unsigned int iei;
	for(iei=0;iei<m_aElemIntp.size();iei++){
		if( m_aElemIntp[iei].id_ea == id_ea ){ break; }
	}
	if( iei == m_aElemIntp.size() ){ return 0; }
	const CElemInterpolation& ei = m_aElemIntp[iei];
	return ei.ilayer;
}

// 最大値最小値を取得する関数
void CField::GetMinMaxValue(double& min, double& max, const CFieldWorld& world, 
							unsigned int idof, int fdt) const
{
	min = 0; max = 0;
	const unsigned int ndof = this->GetNLenValue();
	assert( idof < ndof );
	if( idof >= ndof ) return;
	double *pVal = new double [ndof];
	if( this->m_na_c.id_na_va != 0 ){
		assert( this->IsNodeSeg(CORNER,true,world,fdt) );
		const CNodeAry::CNodeSeg& ns_c = this->GetNodeSeg(CORNER,true,world,fdt);
		{
			ns_c.GetValue(0,pVal);
			const double val = pVal[idof];
			min = val; max = val;
		}
		const unsigned int nnode = ns_c.Size();
		for(unsigned int inode=0;inode<nnode;inode++){
			ns_c.GetValue(inode,pVal);
			double val = pVal[idof];
			min = ( val < min ) ? val : min;
			max = ( val > max ) ? val : max;
		}
	}
	if( this->m_na_b.id_na_va != 0 ){
		assert( this->IsNodeSeg(BUBBLE,true,world,fdt) );
		const CNodeAry::CNodeSeg& ns_b = this->GetNodeSeg(BUBBLE,true,world,fdt);
		if( this->m_na_c.id_na_va == 0 ){
			ns_b.GetValue(0,pVal);
			const double val = pVal[idof];
			min = val; max = val;
		}
		for(unsigned int inode=0;inode<ns_b.Size();inode++){
			ns_b.GetValue(inode,pVal);
			const double val = pVal[idof];
			min = ( val < min ) ? val : min;
			max = ( val > max ) ? val : max;
		}
	}
/*	if( this->GetFieldDerivativeType() & fdt ){
		const CNodeAry::CNodeSeg& ns_c = this->GetNodeSeg(CORNER,true,world,fdt);
		ns_c.GetValue(0,val);
		min = val; max = val;
		for(unsigned int inode=0;inode<ns_c.GetNnode();inode++){
			ns_c.GetValue(0,val);
			min = ( val < min ) ? val : min;
			max = ( val > max ) ? val : max;
		}
	}*/
	delete pVal;
}


// 場の追加
// 古い場を見て足りないNSだけを追加するようにしたい．
bool CField::SetValueType( FIELD_TYPE field_type, const int fdt, CFieldWorld& world)
{
	if( this->GetIDFieldParent() != 0 ){
		assert(0);
		return false;	// 親フィールドのみに有効
	}

	// CORNER節点についてNAやNSを追加する
	if( this->m_na_c.id_na_va != 0 ){
		Fem::Field::CField::CNodeSegInNodeAry& nsna = this->m_na_c;
		unsigned int id_na = nsna.id_na_va;
		assert( world.IsIdNA(id_na) );
		CNodeAry& na = world.GetNA(id_na);
		std::vector<unsigned int> tmp_id_ns_ary = na.GetFreeSegID(3);
		std::vector< std::pair<unsigned int,CNodeAry::CNodeSeg> > add_ns;
		if( fdt&VALUE ){
			nsna.id_ns_va = tmp_id_ns_ary[0];
			if(      field_type == SCALAR  ){
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(1,"SCAL_VAL")) ); 
			}
			else if( field_type == VECTOR2 ){
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(2,"VEC2_VAL")) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"VEC3_VAL")) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_VAL")) );
			}
			else if( field_type == ZSCALAR ){	
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(2,"ZSCAL_VAL")) ); 
			}
			else{
				assert(0);
			}
		}
		if( fdt&VELOCITY ){
			nsna.id_ns_ve = tmp_id_ns_ary[1];
			if(      field_type == SCALAR  ){	
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(1,"SCAL_VELO")) ); 
			}
			else if( field_type == VECTOR2 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(2,"VEC2_VELO")) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(3,"VEC3_VELO")) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_VELO")) );
			}
		}
		if( fdt&ACCELERATION ){
			nsna.id_ns_ac = tmp_id_ns_ary[2];
			if(      field_type == SCALAR  ){	
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(1,"SCAL_ACC")) ); 
			}
			else if( field_type == VECTOR2 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(2,"VEC2_ACC")) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(3,"VEC3_ACC")) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_ACC")) );
			}
		}
		std::vector<int> id_ary = na.AddSegment( add_ns, 0.0 );
	}
	// BUBBLE節点についてNAとNSを追加する
	if( this->m_na_b.id_na_va != 0 ){
		Fem::Field::CField::CNodeSegInNodeAry& nsna = this->m_na_b;
//		unsigned int id_na = nsna.id_na_va;
		assert( world.IsIdNA(nsna.id_na_va) );
		CNodeAry& na = world.GetNA(nsna.id_na_va);
		std::vector<unsigned int> tmp_id_ns_ary = na.GetFreeSegID(3);
		std::vector< std::pair<unsigned int,CNodeAry::CNodeSeg> > add_ns;
		if( fdt & VALUE ){
			nsna.id_ns_va = tmp_id_ns_ary[0];
			if( field_type == SCALAR ){	
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(1,"SCAL_VAL")) ); 
			}
			else if( field_type == VECTOR2 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(2,"VEC2_VAL")) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"VEC3_VAL")) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_VELO")) );
			}
		}
		if( fdt & VELOCITY ){
			nsna.id_ns_ve = tmp_id_ns_ary[1];
			if(      field_type == SCALAR  ){
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(1,"SCALAR_VELO")) ); 
			}
			else if( field_type == VECTOR2 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(2,"VEC2_VELO"  )) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ve, CNodeAry::CNodeSeg(3,"VEC3_VELO"  )) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_VELO" )) );
			}
		}
		if( fdt & ACCELERATION ){
			nsna.id_ns_ac = tmp_id_ns_ary[2];
			if( field_type == SCALAR ){	
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(1,"SCAL_ACC" )) ); 
			}
			else if( field_type == VECTOR2 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(2,"VEC2_ACC" )) );	
			}
			else if( field_type == VECTOR3 ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_ac, CNodeAry::CNodeSeg(3,"VEC3_ACC" )) );	
			}
			else if( field_type == STSR2   ){ 
				add_ns.push_back( std::make_pair(nsna.id_ns_va, CNodeAry::CNodeSeg(3,"STSR2_ACC")) );
			}
		}
		std::vector<int> id_ary = na.AddSegment( add_ns, 0.0 );
	}

	this->m_field_type = field_type;
	this->m_field_derivative_type = fdt;

	unsigned int ndofsize = 0;
	{
		if(      field_type == SCALAR  ) ndofsize = 1;
		else if( field_type == VECTOR2 ) ndofsize = 2;
		else if( field_type == VECTOR3 ) ndofsize = 3;
		else if( field_type == STSR2   ) ndofsize = 3;
		else if( field_type == ZSCALAR ) ndofsize = 2;
		else{ assert(0); }
	}
	this->m_DofSize = ndofsize;
	return true;
}




void CField::SetValueRandom(CFieldWorld& world) const
{
	srand(0);
	if( m_na_c.id_na_va != 0 ){
		assert( world.IsIdNA(m_na_c.id_na_va) );
		CNodeAry& na = world.GetNA(m_na_c.id_na_va);
		if( !na.IsSegID(m_na_c.id_ns_va) ){
			std::cout << "Valueセグメントがない（速度場に設定しようとしている)" << std::endl;
			std::cout << "そのうちValueセグメントを追加で作る関数を加える" << std::endl;
			assert(0);
		}
		assert( na.IsSegID(m_na_c.id_ns_va) );
		CNodeAry::CNodeSeg& ns_val = na.GetSeg(m_na_c.id_ns_va);
		const unsigned int nnode = na.Size();
		for(unsigned int inode=0;inode<nnode;inode++){
			ns_val.SetValue(inode,0,rand()*2.0/(1.0+RAND_MAX)-1.0);
		}
	}
	if( m_na_e.id_na_va != 0 ){
		assert( world.IsIdNA(m_na_e.id_na_va) );
		CNodeAry& na = world.GetNA(m_na_e.id_na_va);
		assert( na.IsSegID(m_na_e.id_ns_va) );
		CNodeAry::CNodeSeg& ns_val = na.GetSeg(m_na_e.id_ns_va);
		const unsigned int nnode = na.Size();
		for(unsigned int inode=0;inode<nnode;inode++){
			ns_val.SetValue(inode,0,rand()*2.0/(1.0+RAND_MAX)-1.0);
		}
	}
	if( m_na_b.id_na_va != 0 ){
		assert( world.IsIdNA(m_na_b.id_na_va) );
		CNodeAry& na = world.GetNA(m_na_b.id_na_va);
		assert( na.IsSegID(m_na_b.id_ns_va) );
		CNodeAry::CNodeSeg& ns_val = na.GetSeg(m_na_b.id_ns_va);
		const unsigned int nnode = na.Size();
		for(unsigned int inode=0;inode<nnode;inode++){
			ns_val.SetValue(inode,0,rand()*2.0/(1.0+RAND_MAX)-1.0);
		}
	}
}

// 数式をセットする
bool CField::SetValue(std::string str_exp, unsigned int idofns, FIELD_DERIVATION_TYPE fdt,
					  CFieldWorld& world, bool is_save)
{		
	assert( idofns < m_DofSize );
	if( idofns >= m_DofSize ){ assert(0); return false; }
	if( fdt != VALUE && fdt != VELOCITY && fdt != ACCELERATION ){ assert(0); return false; }
	if( !(this->GetFieldDerivativeType() & VALUE) && 
		!(this->GetFieldDerivativeType() & VELOCITY) && 
		!(this->GetFieldDerivativeType() & ACCELERATION) ){ assert(0); return false; }
	////////////////
	CValueFieldDof vfd(str_exp);
	////////////////
	if( !this->SetValue(0.0,idofns,fdt,vfd,world) ) return false;
	if(      fdt == VALUE ){
		if( is_save ){	// FieldValueExecの度に設定される量なら
			if( this->m_aValueFieldDof.size()<m_DofSize ){
				this->m_aValueFieldDof.resize(m_DofSize);
			}
			m_aValueFieldDof[idofns] = vfd;
		}
		else{
			if( this->m_aValueFieldDof.size() >= m_DofSize ){
				m_aValueFieldDof[idofns].itype = 0;
			}
		}
	}
	else if( fdt == VELOCITY ){
		if( is_save ){
			if( this->m_aValueFieldDof.size() < m_DofSize*2 ){
				this->m_aValueFieldDof.resize(m_DofSize*2);
			}
			m_aValueFieldDof[idofns+m_DofSize] = vfd;
		}
	}
	else if( fdt == ACCELERATION ){
		if( is_save ){
			if( this->m_aValueFieldDof.size() < m_DofSize*3 ){
				this->m_aValueFieldDof.resize(m_DofSize*3);
			}
			m_aValueFieldDof[idofns+m_DofSize*2] = vfd;
		}
	}
	return true;
}

// 値をセットする
void CField::SetValue(double val, unsigned int idofns, FIELD_DERIVATION_TYPE fdt,
					  CFieldWorld& world, bool is_save)
{
	assert( idofns < m_DofSize );
	if( idofns >= m_DofSize ){ assert(0); return; }
	if( fdt != VALUE && fdt != VELOCITY && fdt != ACCELERATION ){ assert(0); return; }
	if( !(this->GetFieldDerivativeType() & VALUE ) &&
		!(this->GetFieldDerivativeType() & VELOCITY ) && 
		!(this->GetFieldDerivativeType() & ACCELERATION ) ){ assert(0); return; }
	////////////////
	CValueFieldDof vfd;
	vfd.itype = 1;	// value_type
	vfd.val = val;
	////////////////
	this->SetValue(0.0,idofns,fdt,vfd,world); // time=0
	if(      fdt == VALUE ){
		if( is_save ){
			if( this->m_aValueFieldDof.size() < m_DofSize ){
				this->m_aValueFieldDof.resize(m_DofSize);
			}
			m_aValueFieldDof[idofns] = vfd;
		}
		else{
			if( this->m_aValueFieldDof.size() >= m_DofSize ){
				m_aValueFieldDof[idofns].itype = 0;
			}
		}
	}
	else if( fdt == VELOCITY ){
		if( is_save ){
			if( this->m_aValueFieldDof.size() < m_DofSize*2 ){
				this->m_aValueFieldDof.resize(m_DofSize*2);
			}
			m_aValueFieldDof[idofns+m_DofSize] = vfd;
		}
		else{
			if( this->m_aValueFieldDof.size() >= m_DofSize*2 ){
				m_aValueFieldDof[idofns+m_DofSize].itype = 0;
			}
		}
	}
	else if( fdt == ACCELERATION ){
		if( is_save ){
			if( this->m_aValueFieldDof.size() < m_DofSize*3 ){
				this->m_aValueFieldDof.resize(m_DofSize*3);
			}
			m_aValueFieldDof[idofns+m_DofSize*2] = vfd;
		}
		else{
			if( this->m_aValueFieldDof.size() >= m_DofSize*3 ){
				m_aValueFieldDof[idofns+m_DofSize*2].itype = 0;
			}
		}
	}
}

void CField::SetVelocity(unsigned int id_field, CFieldWorld& world){
	if( !world.IsIdField(id_field) ){
		assert(0);
		return;
	}
	if( !(this->GetFieldDerivativeType() & VELOCITY) ){
		assert(0);
		return;
	}

	const CField& field_rhs = world.GetField(id_field);
	{
		const unsigned int id_na_rhs = field_rhs.GetNodeSegInNodeAry(CORNER).id_na_va;
		const unsigned int id_na_lhs = this->GetNodeSegInNodeAry(CORNER).id_na_va;
		if( id_na_rhs != id_na_lhs ){
			assert(0);
			return;
		}
		const CNodeAry::CNodeSeg ns_rhs = field_rhs.GetNodeSeg(CORNER,true,world,VELOCITY);
		CNodeAry::CNodeSeg ns_lhs = this->GetNodeSeg(CORNER,true,world,VELOCITY);
		const unsigned int ndimval_rhs = ns_rhs.Length();
		const unsigned int ndimval_lhs = ns_rhs.Length();
		if(  ndimval_rhs != ndimval_lhs ){
			assert(0);
			return;
		}
		for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
			unsigned int id_ea = m_aElemIntp[iei].id_ea;
			assert( world.IsIdEA(id_ea) );
			const CElemAry& ea = world.GetEA(id_ea);
			assert( ea.IsSegID(m_aElemIntp[iei].id_es_c_va) );
			const CElemAry::CElemSeg& es = ea.GetSeg(m_aElemIntp[iei].id_es_c_va);
			const unsigned int nelem = ea.Size();
			const unsigned int nnoes = es.Length();
			unsigned int node_es[64];
			double value_rhs[64];
			for(unsigned int ielem=0;ielem<nelem;ielem++){
				es.GetNodes(ielem,node_es);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					const unsigned int inode0 = node_es[inoes];
					ns_rhs.GetValue(inode0,value_rhs);
					for(unsigned int idim=0;idim<ndimval_lhs;idim++){
						ns_lhs.SetValue(inode0,idim,value_rhs[idim]);
					}
				}
			}
		}
	}
}

//! 時間依存かどうか調べる
bool CField::IsTimeDependent() const
{
	for(unsigned int idof=0;idof<m_aValueFieldDof.size();idof++){
		const CValueFieldDof& val = m_aValueFieldDof[idof];
		if( val.IsTimeDependent() ){ return true; }
	}
	return false;
}

bool CField::ExecuteValue(double time, CFieldWorld& world)
{
	if( m_DofSize == 0 ){ // BASE field
		return true;
	}
	if( m_is_gradient ){
		assert( world.IsIdField(m_id_field_dep) );
		this->SetGradientValue(m_id_field_dep,world);
		return true;
	}
	unsigned int ifd = m_aValueFieldDof.size()  / m_DofSize;
	if( ifd >= 1 ){	// 値の設定
		for(unsigned int idof=0;idof<m_DofSize;idof++){
			this->SetValue(time,idof,VALUE,m_aValueFieldDof[idof],world);
		}
	}
	if( ifd >= 2 ){	// 速度の設定
		for(unsigned int idof=0;idof<m_DofSize;idof++){
			this->SetValue(time,idof,VELOCITY,m_aValueFieldDof[idof+m_DofSize],world);
		}
	}
	if( ifd >= 3 ){	// 加速度の設定
		assert( ifd == 3 );
		for(unsigned int idof=0;idof<m_DofSize;idof++){
			this->SetValue(time,idof,ACCELERATION,m_aValueFieldDof[idof+m_DofSize*2],world);
		}
	}
	return true;
}

// セーブされた値をidofnsの自由度にセットする
bool CField::SetValue(double time, unsigned int idofns, FIELD_DERIVATION_TYPE fdt, const CValueFieldDof& vfd, CFieldWorld& world)
{
	if( idofns >= this->m_DofSize ) return false;
	if( vfd.itype == 0 ) return false;
	if( !(this->GetFieldDerivativeType() & fdt) ) return false;

	bool is_const_val = false;
    double const_val= 0;
	if(      vfd.itype == 1 ){	// 定数
		is_const_val = true;
		const_val = vfd.val;
	}
	else if( vfd.itype == 2 ){ // 数式
		CEval eval;
		{
			eval.SetKey("x",0.0 );
			eval.SetKey("y",0.0 );
			eval.SetKey("z",0.0 );
			eval.SetKey("t",time);
			if( !eval.SetExp(vfd.math_exp) ) return false;
		}
		if( !eval.IsKeyUsed("x") && !eval.IsKeyUsed("y") && !eval.IsKeyUsed("z") ){
			is_const_val = true;
			const_val = eval.Calc();
		}
	}

	////////////////////////////////
	if( is_const_val ){	// 定数
		if( m_na_c.id_na_va != 0 ){
			assert( world.IsIdNA(m_na_c.id_na_va) );
			CNodeAry& na = world.GetNA(m_na_c.id_na_va);			
			unsigned int id_ns = 0;	// target node segment;
			{
				if(      fdt & VALUE        ) id_ns = m_na_c.id_ns_va;
				else if( fdt & VELOCITY     ) id_ns = m_na_c.id_ns_ve;
				else if( fdt & ACCELERATION ) id_ns = m_na_c.id_ns_ac;
				else{ assert(0); }
				assert( na.IsSegID(id_ns) );
				const CNodeAry::CNodeSeg& ns = na.GetSeg(id_ns);
				assert( idofns < ns.Length() );
			}
			if( m_aElemIntp.size() == 0 || !this->IsPartial() ){	// 剛体の場合
				const unsigned int nnode = na.Size();
				CNodeAry::CNodeSeg& ns = na.GetSeg(id_ns);
				for(unsigned int inode=0;inode<nnode;inode++){
					ns.SetValue(inode,idofns,const_val);
				}
			}
			else{
				for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
					unsigned int id_ea = m_aElemIntp[iei].id_ea;
					assert( world.IsIdEA(id_ea) );
					const CElemAry& ea = world.GetEA(id_ea);
					assert( ea.IsSegID(m_aElemIntp[iei].id_es_c_va) );
					na.SetValueToNodeSegment(ea,m_aElemIntp[iei].id_es_c_va,
						id_ns,idofns,const_val);
				}
			}
		}
/*		if( m_na_e.id_na_va != 0 ){
			assert( world.IsIdNA(m_na_e.id_na_va) );
			CNodeAry& na = world.GetNA(m_na_e.id_na_va);
			const CNodeAry::CNodeSeg& ns_val = na.GetSeg(m_na_e.id_ns_va);
			for(unsigned int iei=0;iei<m_aElemIntp.size();iei++){
				unsigned int id_ea = m_aElemIntp[iei].id_ea;
				assert( world.IsIdEA(id_ea) );
				const CElemAry& ea = world.GetEA(id_ea);
				assert( ea.IsSegID( m_aElemIntp[iei].id_es_e_va ) );
				na.SetValueToNodeSegment(ea,m_aElemIntp[iei].id_es_e_va,
					m_na_e.id_ns_va,idofns,const_val);
			}
		}*/
		if( m_na_b.id_na_va != 0 ){
			assert( world.IsIdNA(m_na_b.id_na_va) );
			CNodeAry& na = world.GetNA(m_na_b.id_na_va);
			unsigned int id_ns = 0;	// target node segment;
			{
				if(      fdt & VALUE        ) id_ns = m_na_b.id_ns_va;
				else if( fdt & VELOCITY     ) id_ns = m_na_b.id_ns_ve;
				else if( fdt & ACCELERATION ) id_ns = m_na_b.id_ns_ac;
				else{ assert(0); }
				assert( na.IsSegID(id_ns) );
				const CNodeAry::CNodeSeg& ns = na.GetSeg(id_ns);
				assert( idofns < ns.Length() );
			}
//			const CNodeAry::CNodeSeg& ns_val = na.GetSeg(id_ns);
			for(unsigned int iea=0;iea<m_aElemIntp.size();iea++){
				unsigned int id_ea = m_aElemIntp[iea].id_ea;
				assert( world.IsIdEA(id_ea) );
				const CElemAry& ea = world.GetEA(id_ea);
				assert( ea.IsSegID(m_aElemIntp[iea].id_es_b_va) );
				na.SetValueToNodeSegment(ea,m_aElemIntp[iea].id_es_b_va,
					m_na_b.id_ns_va,idofns,const_val);
			}
		}
	}
	else{ // 座標に依存する数式
		if( m_na_c.id_na_va != 0 ){
			CEval eval;
			{
				eval.SetKey("x",0.0 );
				eval.SetKey("y",0.0 );
				eval.SetKey("z",0.0 );
				eval.SetKey("t",time);
				if( !eval.SetExp(vfd.math_exp) ) return false;
			}
			assert( world.IsIdNA(m_na_c.id_na_va) );
			CNodeAry& na_va = world.GetNA(this->m_na_c.id_na_va);	
			unsigned int id_ns_va;
			{
				if(      fdt & VALUE        ) id_ns_va = m_na_c.id_ns_va;
				else if( fdt & VELOCITY     ) id_ns_va = m_na_c.id_ns_ve;
				else if( fdt & ACCELERATION ) id_ns_va = m_na_c.id_ns_ac;
				else{ assert(0); }
				assert( na_va.IsSegID(id_ns_va) );
				const CNodeAry::CNodeSeg& ns = na_va.GetSeg(id_ns_va);
				assert( idofns < ns.Length() );
			}
			assert( na_va.IsSegID(id_ns_va) );
			CNodeAry::CNodeSeg& ns_va = na_va.GetSeg(id_ns_va);
			assert( world.IsIdNA(m_na_c.id_na_co) );
			CNodeAry& na_co = world.GetNA(this->m_na_c.id_na_co);
			unsigned int id_ns_co = m_na_c.id_ns_co;
			assert( na_co.IsSegID(id_ns_co) );
			const CNodeAry::CNodeSeg& ns_co = na_co.GetSeg(id_ns_co);
			const unsigned int ndim = this->GetNDimCoord();
			assert( ns_co.Length() == ndim );
			assert( ndim <= 3 );
			double coord[3];
			if( !this->IsPartial() ){	// 親フィールドなら節点を全部参照している。
				for(unsigned int inode=0;inode<na_va.Size();inode++){
					unsigned int inode_co = this->GetMapVal2Co(inode);
					ns_co.GetValue(inode_co,coord);
					switch(ndim){
					case 3: eval.SetKey("z",coord[2]);
					case 2: eval.SetKey("y",coord[1]);
					case 1: eval.SetKey("x",coord[0]);
						break;
					default:
						assert(0);
						break;
					}
					double val = eval.Calc();
					ns_va.SetValue(inode,idofns,val);
				}
			}
			else{	// 要素に参照される節点だけを指している。
				for(unsigned int iei=0;iei<this->m_aElemIntp.size();iei++){
					const CField::CElemInterpolation& ei = m_aElemIntp[iei];
					unsigned int id_ea = ei.id_ea;
					CElemAry& ea = world.GetEA(id_ea);
					assert( ea.IsSegID(ei.id_es_c_va) );
					const CElemAry::CElemSeg& es_c_va = ea.GetSeg(ei.id_es_c_va);
					const unsigned int nnoes = es_c_va.Length();
					unsigned int noes[16];
					for(unsigned int ielem=0;ielem<ea.Size();ielem++){
						es_c_va.GetNodes(ielem,noes);
						for(unsigned int inoes=0;inoes<nnoes;inoes++){
							const unsigned int inode0 = noes[inoes];
							unsigned int inode_co0 = this->GetMapVal2Co(inode0);
							ns_co.GetValue(inode_co0,coord);
							switch(ndim){
							case 3: eval.SetKey("z",coord[2]);
							case 2: eval.SetKey("y",coord[1]);
							case 1: eval.SetKey("x",coord[0]);
								break;
							default:
								assert(0);
								break;
							}
							double val = eval.Calc();
							ns_va.SetValue(inode0,idofns,val);
						}
					}
				}
			}
		}
		if( m_na_b.id_na_va != 0 ){		
			CEval eval;
			{
				eval.SetKey("x",0.0 );
				eval.SetKey("y",0.0 );
				eval.SetKey("z",0.0 );
				eval.SetKey("t",time);
				if( !eval.SetExp(vfd.math_exp) ) return false;
			}
			assert( world.IsIdNA(m_na_b.id_na_va) );
			CNodeAry& na_va = world.GetNA(this->m_na_b.id_na_va);	
			unsigned int id_ns;
			{
				if(      fdt & VALUE        ) id_ns = m_na_b.id_ns_va;
				else if( fdt & VELOCITY     ) id_ns = m_na_b.id_ns_ve;
				else if( fdt & ACCELERATION ) id_ns = m_na_b.id_ns_ac;
				else{ assert(0); }
			}
			assert( na_va.IsSegID(id_ns) );
			CNodeAry::CNodeSeg& ns_va = na_va.GetSeg(id_ns);
			if( !this->IsPartial() && this->m_na_b.id_na_co ){ // 親フィールドなら節点を全部参照している。
				std::cout << "Error!-->Not Implimented" << std::endl;
				assert(0);
				for(unsigned int inode=0;inode<na_va.Size();inode++){
					double val = eval.Calc();
					ns_va.SetValue(inode,idofns,val);
				}
			}
			else{
				for(unsigned int iei=0;iei<this->m_aElemIntp.size();iei++){
					const CField::CElemInterpolation& ei = m_aElemIntp[iei];
					unsigned int id_ea = ei.id_ea;
					assert( world.IsIdEA(id_ea) );
					CElemAry& ea = world.GetEA(id_ea);
					////////////////
					assert( ea.IsSegID(ei.id_es_b_va) );
					const CElemAry::CElemSeg& es_b_va = ea.GetSeg(ei.id_es_b_va);
					assert( es_b_va.Length() == 1);
					////////////////
					assert( ea.IsSegID(ei.id_es_c_co) );
					const CElemAry::CElemSeg& es_c_co = ea.GetSeg(ei.id_es_c_co);
					const unsigned int nnoes = es_c_co.Length();
					////////////////
					const CNodeAry& na_c_co = world.GetNA(m_na_c.id_na_co);
					assert( na_c_co.IsSegID(m_na_c.id_ns_co) );
					const CNodeAry::CNodeSeg& ns_c_co = na_c_co.GetSeg(m_na_c.id_ns_co);
					const unsigned int ndim = ns_c_co.Length();
					////////////////
					unsigned int noes_c[16];
					double coord[3], coord_cnt[3];
					unsigned int inoes_b;
					for(unsigned int ielem=0;ielem<ea.Size();ielem++){
						// 要素の中心座標を求める
						es_c_co.GetNodes(ielem,noes_c);
						es_b_va.GetNodes(ielem,&inoes_b);
						for(unsigned int idim=0;idim<ndim;idim++){ coord_cnt[idim] = 0.0; }
						for(unsigned int inoes=0;inoes<nnoes;inoes++){
							const unsigned int inode0 = noes_c[inoes];
							ns_c_co.GetValue(inode0,coord);
							for(unsigned int idim=0;idim<ndim;idim++){ coord_cnt[idim] += coord[idim]; }
						}
						for(unsigned int idim=0;idim<ndim;idim++){ coord_cnt[idim] /= nnoes; }
						////////////////
						switch(ndim){
						case 3: eval.SetKey("z",coord_cnt[2]);
						case 2: eval.SetKey("y",coord_cnt[1]);
						case 1: eval.SetKey("x",coord_cnt[0]);
							break;
						default:
							assert(0);
							break;
						}
						double val = eval.Calc();
						ns_va.SetValue(inoes_b,idofns,val);
					}
				}
			}
		}
		if( m_na_e.id_na_va != 0 ){
			std::cout << "Error!-->Not Implimented" << std::endl;
			assert(0);
			getchar();
		}
	}
	return true;
}
	
// elem_aryに含まれる節点全てidofblk番目の自由度を固定する
bool Fem::Field::CField::SetBCFlagToES(MatVec::CBCFlag& bc_flag, 
        const Fem::Field::CElemAry& ea, unsigned int id_es, unsigned int idofblk) const
{
  assert( (int)idofblk < bc_flag.LenBlk() );
  if( (int)idofblk >= bc_flag.LenBlk() ) return false;
	assert( ea.IsSegID(id_es) );
	const Fem::Field::CElemAry::CElemSeg& es = ea.GetSeg(id_es);
	unsigned int noes[256];
	unsigned int nnoes = es.Length();
	for(unsigned int ielem=0;ielem<ea.Size();ielem++){
		es.GetNodes(ielem,noes);
		for(unsigned int inoes=0;inoes<nnoes;inoes++){
			const unsigned int inode0 = noes[inoes];
      bc_flag.SetBC(inode0,idofblk);
//			m_Flag[inode0*m_lenBlk+idofblk] = 1;
		}
	}
	return true;
}


void CField::BoundaryCondition(const ELSEG_TYPE& elseg_type, unsigned int idofns, MatVec::CBCFlag& bc_flag, const CFieldWorld& world) const
{
  assert( (int)idofns < bc_flag.LenBlk()  );
	if( m_aElemIntp.size() == 0 ){
		const unsigned int nblk = bc_flag.NBlk();
		for(unsigned int iblk=0;iblk<nblk;iblk++){
			bc_flag.SetBC(iblk,idofns);
		}
		return;
	}
	for(unsigned int iea=0;iea<m_aElemIntp.size();iea++){
		unsigned int id_ea = m_aElemIntp[iea].id_ea;
		const CElemAry& ea = world.GetEA(id_ea);
		if( elseg_type == CORNER && m_aElemIntp[iea].id_es_c_va != 0 ){
			this->SetBCFlagToES(bc_flag, ea, m_aElemIntp[iea].id_es_c_va,idofns);
		}
		if( elseg_type == BUBBLE && m_aElemIntp[iea].id_es_b_va != 0 ){
			this->SetBCFlagToES(bc_flag, ea, m_aElemIntp[iea].id_es_b_va,idofns);
		}
		if( elseg_type == EDGE   && m_aElemIntp[iea].id_es_e_va != 0 ){
			this->SetBCFlagToES(bc_flag, ea, m_aElemIntp[iea].id_es_e_va,idofns);
		}
	}
}


void CField::BoundaryCondition(
		const ELSEG_TYPE& elseg_type, 
        MatVec::CBCFlag& bc_flag, 
		const CFieldWorld& world, 
		unsigned int ioffset ) const
{
	if( m_aElemIntp.size() == 0 && elseg_type == CORNER ){
		const unsigned int nblk = bc_flag.NBlk();
		const unsigned int len = bc_flag.LenBlk();
		for(unsigned int iblk=0;iblk<nblk;iblk++){
		for(unsigned int ilen=0;ilen<len;ilen++){
			bc_flag.SetBC(iblk,ilen+ioffset);
		}
		}
		return;
	}

	{	// Assert
		const unsigned int len = bc_flag.LenBlk();
		assert( ioffset < len );
	}
	const unsigned int nlen = this->GetNLenValue();

	for(unsigned int iea=0;iea<m_aElemIntp.size();iea++){
		unsigned int id_ea = m_aElemIntp[iea].id_ea;
		const CElemAry& ea = world.GetEA(id_ea);
		unsigned int noes[256];
		if( elseg_type == CORNER && m_aElemIntp[iea].id_es_c_va != 0 ){
			const Fem::Field::CElemAry::CElemSeg& es = this->GetElemSeg(id_ea,CORNER,true,world);
			unsigned int nnoes = es.Length();
			for(unsigned int ielem=0;ielem<ea.Size();ielem++){
				es.GetNodes(ielem,noes);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					for(unsigned int ilen=0;ilen<nlen;ilen++){
						bc_flag.SetBC(noes[inoes],ilen+ioffset);
					}
				}
			}
		}
		if( elseg_type == BUBBLE && m_aElemIntp[iea].id_es_b_va != 0 ){
			const Fem::Field::CElemAry::CElemSeg& es = this->GetElemSeg(id_ea,BUBBLE,true,world);
			unsigned int nnoes = es.Length();
			for(unsigned int ielem=0;ielem<ea.Size();ielem++){
				es.GetNodes(ielem,noes);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					for(unsigned int ilen=0;ilen<nlen;ilen++){
						bc_flag.SetBC(noes[inoes],ilen+ioffset);
					}
				}
			}
		}
		if( elseg_type == EDGE   && m_aElemIntp[iea].id_es_e_va != 0 ){
			const Fem::Field::CElemAry::CElemSeg& es = this->GetElemSeg(id_ea,EDGE,true,world);
			unsigned int nnoes = es.Length();
			for(unsigned int ielem=0;ielem<ea.Size();ielem++){
				es.GetNodes(ielem,noes);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					for(unsigned int ilen=0;ilen<nlen;ilen++){
						bc_flag.SetBC(noes[inoes],ilen+ioffset);
					}
				}
			}
		}
	}
}

// 要素補間のタイプを取得する
INTERPOLATION_TYPE CField::GetInterpolationType(unsigned int id_ea,const CFieldWorld& world) const{
	unsigned int iei;
	for(iei=0;iei<m_aElemIntp.size();iei++){
		if( m_aElemIntp[iei].id_ea == id_ea ) break;
	}
	assert( iei != m_aElemIntp.size() );
	if( iei == m_aElemIntp.size() ) throw;
	CElemInterpolation ei = m_aElemIntp[iei];
	ELEM_TYPE elem_type;
	{
		const unsigned int id_ea = ei.id_ea;
		assert( world.IsIdEA(id_ea) );
		const CElemAry& ea = world.GetEA(id_ea);
		elem_type = ea.ElemType();
	}
	if( elem_type == LINE ){
		if( ei.id_es_c_va != 0 && ei.id_es_b_va == 0 && ei.id_es_e_va==0 ){	     return LINE11; }	// 直線一次補間
		else{
			std::cout << "Error!-->Interpolation Not Defined(Tri)" << std::endl;
			std::cout << ei.id_es_c_va << " " << ei.id_es_b_va << std::endl;
			assert(0);
		}
	}
	else if( elem_type == TRI ){
		if(      ei.id_es_c_va != 0 && ei.id_es_b_va == 0 && ei.id_es_e_va==0 ){ return TRI11;   }	// 三角形一次補間
		else if( ei.id_es_c_va != 0 && ei.id_es_b_va != 0 && ei.id_es_e_va==0 ){ return TRI1011; }	// 三角形バブル補間(サブパラメトリック)
		else if( ei.id_es_c_va == 0 && ei.id_es_b_va != 0 && ei.id_es_e_va==0 ){ return TRI1001; }	// 三角形要素一定補間
		else{
			std::cout << "Error!-->Interpolation Not Defined(Tri)" << std::endl;
			std::cout << ei.id_es_c_va << " " << ei.id_es_b_va << std::endl;
			assert(0);
		}
	}
	else if( elem_type == TET ){
		if(      ei.id_es_c_va!=0 && ei.id_es_b_va == 0 && ei.id_es_e_va==0 ){ return TET11;   }	// 四面体一次補間
		else if( ei.id_es_c_va==0 && ei.id_es_b_va != 0 && ei.id_es_e_va==0 ){ return TET1001; }	// 四面体要素一定補間
	}
	else if( elem_type == HEX ){
		if(      ei.id_es_c_va!=0 && ei.id_es_b_va==0 && ei.id_es_e_va==0 ){ return HEX11;   }	// 六面体一次補間
		else if( ei.id_es_c_va==0 && ei.id_es_b_va!=0 && ei.id_es_e_va==0 ){ return HEX1001; }	// 六面体要素一定補間
	}
	std::cout << iei << "   " << ei.id_es_c_va << " " << ei.id_es_b_va << " " << ei.id_es_e_va << std::endl;
	std::cout << "Error!-->Not Implimented : " << elem_type << std::endl;
	assert(0);
	return TRI11;
}

// 三角形の面積を求める関数
static double TriArea2D(const double p0[], const double p1[], const double p2[]){
	return 0.5*( (p1[0]-p0[0])*(p2[1]-p0[1])-(p2[0]-p0[0])*(p1[1]-p0[1]) );
}

// TODO: 一度この関数を呼んだら，次に呼んだ時は高速に処理されるように，ハッシュを構築する
// TODO: 座標やコネクティビティの変更があった場合は，ハッシュを削除する
bool CField::FindVelocityAtPoint(double velo[],  
	unsigned int& id_ea_stat, unsigned int& ielem_stat, double& r1, double& r2,
	const double co[], const Fem::Field::CFieldWorld& world) const 
{
	const Fem::Field::CNodeAry::CNodeSeg& ns_v = this->GetNodeSeg(CORNER,true, world,VELOCITY);
	const Fem::Field::CNodeAry::CNodeSeg& ns_c = this->GetNodeSeg(CORNER,false,world,VELOCITY);
	assert( ns_v.Length() == 2 );
	assert( ns_c.Length() == 2 );
	for(unsigned int iiea=0;iiea<m_aElemIntp.size();iiea++)
	{
		unsigned int id_ea = m_aElemIntp[iiea].id_ea;
		const Fem::Field::CElemAry::CElemSeg& es_v = this->GetElemSeg(id_ea,CORNER,true, world);
		const Fem::Field::CElemAry::CElemSeg& es_c = this->GetElemSeg(id_ea,CORNER,false,world);
		const unsigned int nnoes = 3;		
		assert( es_v.Length() == nnoes );
		assert( es_c.Length() == nnoes );
		assert( es_v.Size() == es_c.Size() );
		const unsigned int nelem = es_v.Size();
		for(unsigned int ielem=0;ielem<nelem;ielem++)
		{
			unsigned int noes_c[nnoes];
			es_c.GetNodes(ielem,noes_c);
			double ec[nnoes][2];
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
				const unsigned int ino = noes_c[inoes];
				ns_c.GetValue(ino,ec[inoes]);
			}
			////////////////
			const double at = TriArea2D(ec[0],ec[1],ec[2]);
            const double a0 = TriArea2D(co,ec[1],ec[2]);    if( a0 < -at*1.0e-3 ) continue;
            const double a1 = TriArea2D(co,ec[2],ec[0]);    if( a1 < -at*1.0e-3 ) continue;
            const double a2 = TriArea2D(co,ec[0],ec[1]);    if( a2 < -at*1.0e-3 ) continue;
			////////////////
			unsigned int noes_v[nnoes];
			es_v.GetNodes(ielem,noes_v);
			double ev[nnoes][2];
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
				const unsigned int ino = noes_v[inoes];
				ns_v.GetValue(ino,ev[inoes]);
			}
			////////////////
			velo[0] = (a0*ev[0][0] + a1*ev[1][0] + a2*ev[2][0])/at;
			velo[1] = (a0*ev[0][1] + a1*ev[1][1] + a2*ev[2][1])/at;
			id_ea_stat = id_ea;
			ielem_stat = ielem; 
			r1 = a1/at;
			r2 = a2/at;
			return true;
		}
	}
	id_ea_stat = 0;
	ielem_stat = 0;
	r1 = 0;
	r2 = 0;
	return false;
}

// 勾配をセットする
bool Fem::Field::CField::SetGradient(unsigned int id_field, Field::CFieldWorld& world, bool is_save)
{
	if( !world.IsIdField(id_field) ) return false;
	this->SetGradientValue(id_field,world);
	if( is_save ){
		m_is_gradient = true;
		m_id_field_dep = id_field;
	}
	return true;
}

bool Fem::Field::CField::SetGradientValue(unsigned int id_field, CFieldWorld& world)
{
	if( !world.IsIdField(id_field) ) return false;
	Fem::Field::CField& field = world.GetField(id_field);

	if( this->m_aElemIntp.size() != 1 ){
		std::cout << "Error!-->Not Implimented" << std::endl;
		getchar();
		assert(0);
	}

	Fem::Field::INTERPOLATION_TYPE type_from, type_to;
	{
		const std::vector<unsigned int>& aIdEA_from = field.GetAry_IdElemAry();
		const std::vector<unsigned int>& aIdEA_to   = this->GetAry_IdElemAry();
		if( aIdEA_from.size() != aIdEA_to.size() ) return false;
		const unsigned int niea = aIdEA_from.size();
		assert( niea == 1 );
		if( aIdEA_from[0] != aIdEA_to[0] ) return false;
		const unsigned int id_ea = aIdEA_from[0];
		type_from = field.GetInterpolationType(id_ea,world);
		type_to   = this->GetInterpolationType(id_ea,world);
	}

	unsigned int nnoes, ndim;
	if( type_from==HEX11 && type_to==HEX1001 ){
		nnoes = 8; ndim = 3;
	}
	else if( type_from==TET11 && type_to==TET1001 ){
		nnoes = 4; ndim = 3;
	}
	else if( type_from==TRI11 && type_to==TRI1001 ){
		nnoes = 3; ndim = 2;
	}
	else{
		std::cout << "NotImplimented!" << std::endl;
		assert(0);
		getchar();
	}

	unsigned int id_ea = this->m_aElemIntp[0].id_ea;
	const CElemAry& ea = world.GetEA(id_ea);
	const CElemAry::CElemSeg& es_c_co = field.GetElemSeg(id_ea,CORNER,false,world);
	const CElemAry::CElemSeg& es_c_va = field.GetElemSeg(id_ea,CORNER,true, world);
	const CElemAry::CElemSeg& es_b_va = this->GetElemSeg(id_ea,BUBBLE,true, world);

	const CNodeSegInNodeAry& nsna_c = field.GetNodeSegInNodeAry(CORNER);
	assert( world.IsIdNA(nsna_c.id_na_co) );
	assert( world.IsIdNA(nsna_c.id_na_va) );
	const CNodeAry& na_c_co = world.GetNA(nsna_c.id_na_co);
	const CNodeAry::CNodeSeg& ns_c_co = na_c_co.GetSeg(nsna_c.id_ns_co);
	const CNodeAry& na_c_va = world.GetNA(nsna_c.id_na_va);
	const CNodeAry::CNodeSeg& ns_c_va = na_c_va.GetSeg(nsna_c.id_ns_va);
	unsigned int id_na_b_va = m_na_b.id_na_va;
	unsigned int id_ns_b_va = m_na_b.id_ns_va;

	assert( world.IsIdNA(id_na_b_va) );
	CNodeAry& na_b_va = world.GetNA(id_na_b_va);
	CNodeAry::CNodeSeg& ns_b_va = na_b_va.GetSeg(id_ns_b_va);

	double coord[16][3];
	double value[16];
	double grad[3];

//	const unsigned int nnoes_c = es_c_co.GetSizeNoes();
	unsigned int noes[64];

	for(unsigned int ielem=0;ielem<ea.Size();ielem++){
		{	// 座標(coord)と値(value)を作る
			es_c_co.GetNodes(ielem,noes);
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
				unsigned int ipoi0 = noes[inoes];
				assert( ipoi0 < na_c_co.Size() );
				ns_c_co.GetValue(ipoi0,coord[inoes]);
			}
/*			if( id_es_c_va == id_es_c_co ){	
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					unsigned int ipoi0 = noes[inoes];
					assert( ipoi0 < na_c_va.Size() );
					na_c_va.GetValueFromNode(ipoi0,id_ns_c_va,0,val);
					value[inoes] = val;
				}
			}
			else{*/
				es_c_va.GetNodes(ielem,noes);
				for(unsigned int inoes=0;inoes<nnoes;inoes++){
					unsigned int ipoi0 = noes[inoes];
					assert( ipoi0 < na_c_va.Size() );
					ns_c_va.GetValue(ipoi0,&value[inoes]);
				}
//			}
		}
		if( type_from == HEX11 ){
			double dndx[8][3];
			double an[8];
			double detjac;
			ShapeFunc_Hex8(0.0,0.0,0.0, coord, detjac,dndx,an);
			for(unsigned int idim=0;idim<ndim;idim++){ grad[idim] = 0.0; }
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
			for(unsigned int idim=0;idim<ndim;idim++){
				grad[idim] += value[inoes]*dndx[inoes][idim];
			}
			}
		}
		else if( type_from == TET11 ){
			double dldx[4][3];
			double const_term[4];
			TetDlDx(dldx,const_term, coord[0],coord[1],coord[2],coord[3]);
			for(unsigned int idim=0;idim<ndim;idim++){ grad[idim] = 0.0; }
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
			for(unsigned int idim=0;idim<ndim;idim++){
				grad[idim] += value[inoes]*dldx[inoes][idim];
			}
			}
		}
		else if( type_from == TRI11 ){
			double dldx[3][2];
			double const_term[3];
			TriDlDx(dldx,const_term, coord[0],coord[1],coord[2]);
			for(unsigned int idim=0;idim<ndim;idim++){ grad[idim] = 0.0; }
			for(unsigned int inoes=0;inoes<nnoes;inoes++){
			for(unsigned int idim=0;idim<ndim;idim++){
				grad[idim] += value[inoes]*dldx[inoes][idim];
			}
			}
			grad[2] = 0.0;
		}
//		std::cout << grad[0] << " " << grad[1] << " " << grad[2] << std::endl;
		{
			double norm = sqrt(grad[0]*grad[0]+grad[1]*grad[1]+grad[2]*grad[2]);
			norm *=0.2;
//			norm = 15.0;
			grad[0] /= norm;
			grad[1] /= norm;
			grad[2] /= norm;
		}
		{
			unsigned int noes[16];
			es_b_va.GetNodes(ielem,noes);
			unsigned int ipoi0 = noes[0];
			assert( ipoi0 < na_b_va.Size() );
			for(unsigned int idim=0;idim<ndim;idim++){
				ns_b_va.SetValue(ipoi0,idim,grad[idim]);
			}
		}
	}
	return true;
}


// MicroAVS inpファイルへの書き出し
bool CField::ExportFile_Inp(const std::string& file_name, const CFieldWorld& world)
{
	if( m_aElemIntp.size() != 1 ){
		std::cout << "未実装" << std::endl;
		assert(0);
	}
	const unsigned int id_ea = m_aElemIntp[0].id_ea;
	if( this->GetInterpolationType(id_ea,world) != HEX1001 && 
		this->GetInterpolationType(id_ea,world) != TET1001  )
	{
		std::cout << "未実装" << std::endl;
		assert(0);
	}

	std::ofstream fout(file_name.c_str());
	fout << 1 << "\n";
	fout << "data\n";
	fout << "step1\n";
	{
		unsigned int id_na_c_co = m_na_c.id_na_co;
		const CNodeAry& na = world.GetNA(id_na_c_co);
		const unsigned int nnode = na.Size();
		unsigned int id_ea = m_aElemIntp[0].id_ea;
		const CElemAry& ea = world.GetEA(id_ea);
		const unsigned int nelem = ea.Size();
		fout << nnode << " " << nelem << "\n";
	}
	{
		unsigned int id_na_c_co = m_na_c.id_na_co;
		unsigned int id_ns_c_co = m_na_c.id_ns_co;
		const CNodeAry& na = world.GetNA(id_na_c_co);
		const CNodeAry::CNodeSeg& ns_c_co = na.GetSeg(id_ns_c_co);
		double coord[3];
		for(unsigned int inode=0;inode<na.Size();inode++){
			fout << inode+1 << " ";
			ns_c_co.GetValue(inode,coord);
			for(unsigned int idim=0;idim<3;idim++){
				fout << coord[idim] << " ";
			}
			fout << "\n";
		}
	}

	const CElemAry& ea = world.GetEA(id_ea);
	std::string str_elem_type;
	{
		if( ea.ElemType() == TET ){
			str_elem_type = "tet";
		}
		else if( ea.ElemType() == HEX ){
			str_elem_type = "hex";
		}
		else{ assert(0); }
	}
	{
		const CElemAry::CElemSeg& es_c_co = this->GetElemSeg(id_ea,CORNER,false,world);
		unsigned int id_na_c_co = m_na_c.id_na_co;
		assert( id_na_c_co == es_c_co.GetIdNA() );
		unsigned int noes_c[16];
		unsigned int nnoes_c = es_c_co.Length();
		for(unsigned int ielem=0;ielem<ea.Size();ielem++){
			es_c_co.GetNodes(ielem,noes_c);
			fout << ielem+1 << " 0 " << str_elem_type << " ";
			for(unsigned int inoes=0;inoes<nnoes_c;inoes++){
				fout << noes_c[inoes]+1 << " ";
			}
			fout << "\n";
		}
	}
	fout << "0 3\n";
	fout << "3 1 1 1\n";
	fout << "hoge_x,\n";
	fout << "hoge_y,\n";
	fout << "hoge_z,\n";
	{
		const CElemAry::CElemSeg& es_b_va = this->GetElemSeg(id_ea,BUBBLE,true,world);
		unsigned int id_na_b_va = m_na_b.id_na_va;
		assert( id_na_b_va == es_b_va.GetIdNA() );
		const CNodeAry& na_b_va = world.GetNA(id_na_b_va);
		unsigned int id_ns_b_va = m_na_b.id_ns_va;
		const CNodeAry::CNodeSeg& ns_b_va = na_b_va.GetSeg(id_ns_b_va);
		unsigned int inoes_b;
		double value[3];
		const unsigned int nnoes = es_b_va.Length();
		assert( nnoes == 1 );
		for(unsigned int ielem=0;ielem<ea.Size();ielem++){
			es_b_va.GetNodes(ielem,&inoes_b);
			ns_b_va.GetValue(inoes_b,value);
			fout << ielem+1 << " " << value[0] << " " << value[1] << " " << value[2] << "\n";
		}
	}
	
	return true;
}
