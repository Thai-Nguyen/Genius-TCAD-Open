/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/



#include "simulation_system.h"
#include "insulator_region.h"
#include "boundary_condition_ii.h"
#include "petsc_utils.h"

using PhysicalUnit::kb;
using PhysicalUnit::e;


///////////////////////////////////////////////////////////////////////
//----------------Function and Jacobian evaluate---------------------//
///////////////////////////////////////////////////////////////////////


/*---------------------------------------------------------------------
 * do pre-process to function for EBM3 solver
 */
void InsulatorInsulatorInterfaceBC::EBM3_Function_Preprocess(PetscScalar *,Vec f, std::vector<PetscInt> &src_row,
    std::vector<PetscInt> &dst_row, std::vector<PetscInt> &clear_row)
{
  // search for all the node with this boundary type
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();

  for(; node_it!=end_it; ++node_it )
  {

    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // buffer for saving regions and fvm_nodes this *node_it involves
    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node, these fvm_nodes have the same location in geometry,
    // but belong to different regions in logic.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      regions.push_back( (*rnode_it).second.first );
      fvm_nodes.push_back( (*rnode_it).second.second );

      // the first insulator region
      if(i==0)
      {}
      // other insulator region
      else
      {
        // record the source row and dst row
        src_row.push_back(fvm_nodes[i]->global_offset());
        dst_row.push_back(fvm_nodes[0]->global_offset());
        clear_row.push_back(fvm_nodes[i]->global_offset());

        if(regions[i]->get_advanced_model()->enable_Tl())
        {
          src_row.push_back(fvm_nodes[i]->global_offset()+1);
          dst_row.push_back(fvm_nodes[0]->global_offset()+1);
          clear_row.push_back(fvm_nodes[i]->global_offset()+1);
        }
      }
    }
  }
}



/*---------------------------------------------------------------------
 * build function and its jacobian for EBM3 solver
 */
void InsulatorInsulatorInterfaceBC::EBM3_Function(PetscScalar * x, Vec f, InsertMode &add_value_flag)
{
  // note, we will use ADD_VALUES to set values of vec f
  // if the previous operator is not ADD_VALUES, we should assembly the vec
  if( (add_value_flag != ADD_VALUES) && (add_value_flag != NOT_SET_VALUES) )
  {
    VecAssemblyBegin(f);
    VecAssemblyEnd(f);
  }

  // buffer for Vec index
  std::vector<PetscInt> iy;
  // buffer for Vec value
  std::vector<PetscScalar> y_new;

  // search for all the node with this boundary type
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(; node_it!=end_it; ++node_it )
  {

    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node, these nodes are the same in geometry,
    // but in different region.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node * fvm_node = (*rnode_it).second.second;
      if(!fvm_node->is_valid()) continue;

      regions.push_back( region );
      fvm_nodes.push_back( fvm_node );

      // we may have several regions with insulator material

      // the first insulator region
      if(i==0)
      {
        // do nothing.
        // however, we will add fvm integral of other regions to it.
      }
      // other insulator regions
      else
      {
        unsigned int node_psi_offset = regions[i]->ebm_variable_offset(POTENTIAL);
        unsigned int node_Tl_offset  = regions[i]->ebm_variable_offset(TEMPERATURE);


        const FVM_Node * ghost_fvm_node = fvm_nodes[0];

        // the governing equation of this fvm node --

        PetscScalar V = x[fvm_nodes[i]->local_offset()+node_psi_offset]; // psi of this node
        PetscScalar V_in = x[fvm_nodes[0]->local_offset()+node_psi_offset]; // psi for ghost node
        // the psi of this node is equal to corresponding psi of node in the first insulator region
        // since psi should be continuous for the interface
        PetscScalar ff1 = V - V_in;
        iy.push_back(fvm_nodes[i]->global_offset()+node_psi_offset);
        y_new.push_back(ff1);

        if(regions[i]->get_advanced_model()->enable_Tl())
        {
          PetscScalar T = x[fvm_nodes[i]->local_offset()+node_Tl_offset]; // T of this node
          PetscScalar T_in = x[fvm_nodes[0]->local_offset()+node_Tl_offset]; // T for ghost node
          // the T of this node is equal to corresponding T of node in the first insulator region
          // by assuming no heat resistance between 2 region
          PetscScalar ff2 = T - T_in;
          iy.push_back(fvm_nodes[i]->global_offset()+node_Tl_offset);
          y_new.push_back(ff2);
        }
      }

    }

  }

  // set new value to row
  if( iy.size() )
    VecSetValues(f, iy.size(), &(iy[0]), &(y_new[0]), ADD_VALUES);

  add_value_flag = INSERT_VALUES;
}







/*---------------------------------------------------------------------
 * do pre-process to jacobian matrix for EBM3 solver
 */
void InsulatorInsulatorInterfaceBC::EBM3_Jacobian_Preprocess(PetscScalar * ,SparseMatrix<PetscScalar> *jac, std::vector<PetscInt> &src_row,
    std::vector<PetscInt> &dst_row, std::vector<PetscInt> &clear_row)
{
  // search for all the node with this boundary type
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();

  for(; node_it!=end_it; ++node_it )
  {

    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    // buffer for saving regions and fvm_nodes this *node_it involves
    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node, these fvm_nodes have the same location in geometry,
    // but belong to different regions in logic.
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);
    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      regions.push_back( (*rnode_it).second.first );
      fvm_nodes.push_back( (*rnode_it).second.second );

      // the first insulator region
      if(i==0)
      {}
      // other insulator region
      else
      {
        // record the source row and dst row
        src_row.push_back(fvm_nodes[i]->global_offset());
        dst_row.push_back(fvm_nodes[0]->global_offset());
        clear_row.push_back(fvm_nodes[i]->global_offset());

        if(regions[i]->get_advanced_model()->enable_Tl())
        {
          src_row.push_back(fvm_nodes[i]->global_offset()+1);
          dst_row.push_back(fvm_nodes[0]->global_offset()+1);
          clear_row.push_back(fvm_nodes[i]->global_offset()+1);
        }
      }
    }
  }
}


/*---------------------------------------------------------------------
 * build function and its jacobian for EBM3 solver
 */
void InsulatorInsulatorInterfaceBC::EBM3_Jacobian(PetscScalar * x, SparseMatrix<PetscScalar> *jac, InsertMode &add_value_flag)
{

  // after that, set values to source rows
  BoundaryCondition::const_node_iterator node_it = nodes_begin();
  BoundaryCondition::const_node_iterator end_it = nodes_end();
  for(node_it = nodes_begin(); node_it!=end_it; ++node_it )
  {
    // skip node not belongs to this processor
    if( (*node_it)->processor_id()!=Genius::processor_id() ) continue;

    std::vector<const SimulationRegion *> regions;
    std::vector<const FVM_Node *> fvm_nodes;

    // search all the fvm_node which has *node_it as root node
    BoundaryCondition::region_node_iterator  rnode_it     = region_node_begin(*node_it);
    BoundaryCondition::region_node_iterator  end_rnode_it = region_node_end(*node_it);

    for(unsigned int i=0 ; rnode_it!=end_rnode_it; ++i, ++rnode_it  )
    {
      const SimulationRegion * region = (*rnode_it).second.first;
      const FVM_Node * fvm_node = (*rnode_it).second.second;
      if(!fvm_node->is_valid()) continue;

      regions.push_back( region );
      fvm_nodes.push_back( fvm_node );


      // the first insulator region
      if(i==0) continue;

      // other insulator region
      else
      {

        unsigned int node_psi_offset = regions[i]->ebm_variable_offset(POTENTIAL);
        unsigned int node_Tl_offset  = regions[i]->ebm_variable_offset(TEMPERATURE);

        //the indepedent variable number, we need 2 here.
        adtl::AutoDScalar::numdir=2;

        // psi of this node
        AutoDScalar  V    = x[fvm_nodes[i]->local_offset() + node_psi_offset]; V.setADValue(0,1.0);

        // psi for ghost node
        AutoDScalar  V_in = x[fvm_nodes[0]->local_offset() + node_psi_offset]; V_in.setADValue(1,1.0);

        // the psi of this node is equal to corresponding psi of insulator node in the first insulator region
        AutoDScalar  ff1 = V - V_in;

        // set Jacobian of governing equation ff
        jac->add( fvm_nodes[i]->global_offset()+node_psi_offset,  fvm_nodes[i]->global_offset()+node_psi_offset,  ff1.getADValue(0) );
        jac->add( fvm_nodes[i]->global_offset()+node_psi_offset,  fvm_nodes[0]->global_offset()+node_psi_offset,  ff1.getADValue(1) );

        if(regions[i]->get_advanced_model()->enable_Tl())
        {
          // T of this node
          AutoDScalar  T = x[fvm_nodes[i]->local_offset()+node_Tl_offset]; T.setADValue(0,1.0);

          // T for insulator node in the first insulator region
          AutoDScalar  T_in = x[fvm_nodes[0]->local_offset()+node_Tl_offset]; T_in.setADValue(1,1.0);

          // the T of this node is equal to corresponding T of insulator node in the first insulator region
          AutoDScalar ff2 = T - T_in;

          // set Jacobian of governing equation ff2
          jac->add( fvm_nodes[i]->global_offset()+node_Tl_offset,  fvm_nodes[i]->global_offset()+node_Tl_offset,  ff2.getADValue(0) );
          jac->add( fvm_nodes[i]->global_offset()+node_Tl_offset,  fvm_nodes[0]->global_offset()+node_Tl_offset,  ff2.getADValue(1) );
        }

      }
    }
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;
}

